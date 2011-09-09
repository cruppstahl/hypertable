#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL=$HT_HOME/bin/hypertable
SCRIPT_DIR=`dirname $0`
#DATA_SEED=42 # for repeating certain runs
DIGEST="openssl dgst -md5"
RS1_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs2.pid
RS3_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs3.pid
RUN_DIR=`pwd`

if [ ${FAST} != "" ] ; then
  echo "Skipping test b/c running in FAST mode"
  exit 0
fi

. $HT_HOME/bin/ht-env.sh

# get rid of all old logfiles
\rm -rf $HT_HOME/log/*

# Dumping cores slows things down unnecessarily for normal test runs
#ulimit -c 0

wait_for_recovery() {
  local id=$1
  s="Leaving RecoverServer $id state=COMPLETE"
  # Test 21 induces an error before the "state=COMPLETE" message is printed,
  # therefore grep through the mml output
  [ "$TEST_21" ] && s="MetaLog::Entity RecoverServer.*payload=\{ state=COMPLETE"
  n=0
  egrep "$s" master.output.$TEST_ID
  while [ $? -ne "0" ]
  do
    (( n += 1 ))
    if [ "$n" -gt "300" ]; then
      echo "wait_for_recovery: time exceeded"
      exit 1
    fi
    sleep 2
    egrep "$s" master.output.$TEST_ID
  done
}

gen_test_data() {
  seed=${DATA_SEED:-$$}
  size=${DATA_SIZE:-"2000000"}
  perl -e 'print "#row\tcolumn\tvalue\n"' > data.header
  perl -e 'srand('$seed'); for($i=0; $i<'$size'; ++$i) {
    printf "row%07d\tcolumn%d\tvalue%d\n", $i, int(rand(3))+1, $i
  }' > data.body
  $DIGEST < data.body > data.md5
}

stop_range_servers() {
  echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38062
  echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38061
  echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38060
  sleep 1
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs?.pid`
}

stop_rs1() {
  echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38060
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs1.pid`
}

stop_rs2() {
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs2.pid`
}

stop_rs3() {
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs3.pid`
}

set_tests() {
  for i in $@; do
    eval TEST_$i=1
  done
}

# Runs an individual test with two RangeServers; the master goes down
# during recovery
run_test1() {
  local TEST_ID=$1
  shift;
  
  echo "Running test $TEST_ID." >> report.txt

  stop_range_servers

  $HT_HOME/bin/start-test-servers.sh --no-master --no-rangeserver \
        --no-thriftbroker --clear

  # start master-launcher script in background. it will restart the
  # master as soon as it crashes
  $SCRIPT_DIR/master-launcher.sh $@ > master.output.$TEST_ID 2>&1 &
  sleep 10

  # give rangeserver time to get registered etc 
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
     --Hypertable.RangeServer.ProxyName=rs1 \
     --Hypertable.RangeServer.Port=38060 \
     --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs1.output.$TEST_ID &
  sleep 5
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
    --Hypertable.RangeServer.ProxyName=rs2 \
    --Hypertable.RangeServer.Port=38061 \
    --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs2.output.$TEST_ID &

  $HT_SHELL --batch < $SCRIPT_DIR/create-test-table.hql
  if [ $? != 0 ] ; then
    echo "Unable to create table 'failover-test', exiting ..."
    exit 1
  fi

  $HT_SHELL --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=100K \
      --batch < $SCRIPT_DIR/load.hql
  if [ $? != 0 ] ; then
    echo "Problem loading table 'failover-test', exiting ..."
    exit 1
  fi
  
  # wait a bit for splits 
  $HT_HOME/bin/ht_rsclient --exec "COMPACT RANGES USER; WAIT FOR MAINTENANCE;"
  sleep 10
  $HT_SHELL --namespace 'sys' --batch --exec \
     'select Location from METADATA revs=1;' > locations.$TEST_ID  
  
  # kill rs1
  stop_rs1
  
  # wait for recovery to complete 
  wait_for_recovery "rs1"
  
  # dump keys
  $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
      | grep -v "hypertable" > dbdump.$TEST_ID
  if [ $? != 0 ] ; then
    echo "Problem dumping table 'failover-test', exiting ..."
    exit 1
  fi
  
  # shut everything down
  stop_rs2
  $HT_HOME/bin/stop-servers.sh

  $DIGEST < dbdump.$TEST_ID > dbdump.md5
  diff data.md5 dbdump.md5 > out
  if [ $? != 0 ] ; then
    echo "Test $TEST_ID FAILED." >> report.txt
    echo "Test $TEST_ID FAILED." >> errors.txt
    cat out >> report.txt
    touch error
    $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
        | grep -v "hypertable" > dbdump.$TEST_ID.again
    if [ $? != 0 ] ; then
        echo "Problem dumping table 'failover-test', exiting ..."
        exit 1
    fi
  else
    echo "Test $TEST_ID PASSED." >> report.txt
  fi
}

# Runs an individual test with three RangeServers; one additional range server
# goes down during recovery, often in combination with a simulated
# crash in the master
run_test2() {
  local TEST_ID=$1
  local MS_IND=$2;
  local RS_IND=$3;
  
  echo "Running test $TEST_ID." >> report.txt

  stop_range_servers

  $HT_HOME/bin/start-test-servers.sh --no-master --no-rangeserver \
        --no-thriftbroker --clear

  # start master-launcher script in background. it will restart the
  # master as soon as it crashes
  $SCRIPT_DIR/master-launcher.sh $MS_IND > master.output.$TEST_ID 2>&1 &
  sleep 10

  # give rangeserver time to get registered etc 
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
     --Hypertable.RangeServer.ProxyName=rs1 \
     --Hypertable.RangeServer.Port=38060 \
     --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs1.output.$TEST_ID &
  sleep 5
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
    --Hypertable.RangeServer.ProxyName=rs2 \
    --Hypertable.RangeServer.Port=38061 $RS_IND \
    --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs2.output.$TEST_ID &
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS3_PIDFILE \
    --Hypertable.RangeServer.ProxyName=rs3 \
    --Hypertable.RangeServer.Port=38062 \
    --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs3.output.$TEST_ID &

  $HT_SHELL --batch < $SCRIPT_DIR/create-test-table.hql
  if [ $? != 0 ] ; then
    echo "Unable to create table 'failover-test', exiting ..."
    exit 1
  fi

  $HT_SHELL --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=100K \
      --batch < $SCRIPT_DIR/load.hql
  if [ $? != 0 ] ; then
    echo "Problem loading table 'failover-test', exiting ..."
    exit 1
  fi
  
  # wait a bit for splits 
  $HT_HOME/bin/ht_rsclient --exec "COMPACT RANGES USER; WAIT FOR MAINTENANCE;"
  sleep 10
  $HT_SHELL --namespace 'sys' --batch --exec \
     'select Location from METADATA revs=1;' > locations.$TEST_ID  
  
  # kill rs1; rs2 will be killed by the error inducer
  stop_rs1
  
  # wait for recovery to complete 
  wait_for_recovery "rs1"
  wait_for_recovery "rs2"
  
  # dump keys
  $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
      | grep -v "hypertable" > dbdump.$TEST_ID
  if [ $? != 0 ] ; then
    echo "Problem dumping table 'failover-test', exiting ..."
    exit 1
  fi
  
  # shut everything down
  stop_rs2
  stop_rs3
  $HT_HOME/bin/stop-servers.sh

  $DIGEST < dbdump.$TEST_ID > dbdump.md5
  diff data.md5 dbdump.md5 > out
  if [ $? != 0 ] ; then
    echo "Test $TEST_ID FAILED." >> report.txt
    echo "Test $TEST_ID FAILED." >> errors.txt
    cat out >> report.txt
    touch error
    $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
        | grep -v "hypertable" > dbdump.$TEST_ID.again
    if [ $? != 0 ] ; then
        echo "Problem dumping table 'failover-test', exiting ..."
        exit 1
    fi
  else
    echo "Test $TEST_ID PASSED." >> report.txt
  fi
}

# Runs an individual test with three RangeServers, and during recovery
# one of them goes down with an error-inducer code. The master stays 
# alive during the whole process
run_test3() {
  local TEST_ID=$1
  local RS_IND=$2;
  
  echo "Running test $TEST_ID." >> report.txt

  stop_range_servers

  $HT_HOME/bin/start-test-servers.sh --no-master --no-rangeserver \
        --no-thriftbroker --clear

  # start master-launcher script in background. it will restart the
  # master as soon as it crashes
  $SCRIPT_DIR/master-launcher.sh > master.output.$TEST_ID 2>&1 &
  sleep 10

  # give rangeserver time to get registered etc 
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
     --Hypertable.RangeServer.ProxyName=rs1 \
     --Hypertable.RangeServer.Port=38060 \
     --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs1.output.$TEST_ID &
  sleep 5
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
    --Hypertable.RangeServer.ProxyName=rs2 \
    --Hypertable.RangeServer.Port=38061 $RS_IND \
    --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs2.output.$TEST_ID &
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS3_PIDFILE \
    --Hypertable.RangeServer.ProxyName=rs3 \
    --Hypertable.RangeServer.Port=38062 \
    --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs3.output.$TEST_ID &

  $HT_SHELL --batch < $SCRIPT_DIR/create-test-table.hql
  if [ $? != 0 ] ; then
    echo "Unable to create table 'failover-test', exiting ..."
    exit 1
  fi

  $HT_SHELL --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=100K \
      --batch < $SCRIPT_DIR/load.hql
  if [ $? != 0 ] ; then
    echo "Problem loading table 'failover-test', exiting ..."
    exit 1
  fi
  
  # wait a bit for splits 
  $HT_HOME/bin/ht_rsclient --exec "COMPACT RANGES USER; WAIT FOR MAINTENANCE;"
  sleep 10
  $HT_SHELL --namespace 'sys' --batch --exec \
     'select Location from METADATA revs=1;' > locations.$TEST_ID  
  
  # kill rs1; rs2 will be killed by the error inducer
  stop_rs1
  
  # wait for recovery to complete 
  wait_for_recovery "rs1"
  wait_for_recovery "rs2"
  
  # dump keys
  $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
      | grep -v "hypertable" > dbdump.$TEST_ID
  if [ $? != 0 ] ; then
    echo "Problem dumping table 'failover-test', exiting ..."
    exit 1
  fi
  
  # shut everything down
  stop_rs2
  stop_rs3
  $HT_HOME/bin/stop-servers.sh

  $DIGEST < dbdump.$TEST_ID > dbdump.md5
  diff data.md5 dbdump.md5 > out
  if [ $? != 0 ] ; then
    echo "Test $TEST_ID FAILED." >> report.txt
    echo "Test $TEST_ID FAILED." >> errors.txt
    cat out >> report.txt
    touch error
    $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
        | grep -v "hypertable" > dbdump.$TEST_ID.again
    if [ $? != 0 ] ; then
        echo "Problem dumping table 'failover-test', exiting ..."
        exit 1
    fi
  else
    echo "Test $TEST_ID PASSED." >> report.txt
  fi
}

# Runs an individual test with three RangeServers, and during recovery
# the master crashes. Before the master comes up again, the second range server
# will go down and force the master to re-create the recovery plan.
run_test4() {
  local TEST_ID=$1
  local MS_IND=$2;
  
  echo "Running test $TEST_ID." >> report.txt

  stop_range_servers

  $HT_HOME/bin/start-test-servers.sh --no-master --no-rangeserver \
        --no-thriftbroker --clear

  # start master-launcher script in background. it will restart the
  # master as soon as it crashes
  $SCRIPT_DIR/master-launcher.sh $MS_IND $RS2_PIDFILE \
      > master.output.$TEST_ID 2>&1 &
  sleep 10

  # give rangeserver time to get registered etc 
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
     --Hypertable.RangeServer.ProxyName=rs1 \
     --Hypertable.RangeServer.Port=38060 \
     --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs1.output.$TEST_ID &
  sleep 5
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
    --Hypertable.RangeServer.ProxyName=rs2 \
    --Hypertable.RangeServer.Port=38061 \
    --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs2.output.$TEST_ID &
  $HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS3_PIDFILE \
    --Hypertable.RangeServer.ProxyName=rs3 \
    --Hypertable.RangeServer.Port=38062 \
    --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs3.output.$TEST_ID &

  $HT_SHELL --batch < $SCRIPT_DIR/create-test-table.hql
  if [ $? != 0 ] ; then
    echo "Unable to create table 'failover-test', exiting ..."
    exit 1
  fi

  $HT_SHELL --Hypertable.Mutator.ScatterBuffer.FlushLimit.PerServer=100K \
      --batch < $SCRIPT_DIR/load.hql
  if [ $? != 0 ] ; then
    echo "Problem loading table 'failover-test', exiting ..."
    exit 1
  fi
  
  # wait a bit for splits 
  $HT_HOME/bin/ht_rsclient --exec "COMPACT RANGES USER; WAIT FOR MAINTENANCE;"
  sleep 10
  $HT_SHELL --namespace 'sys' --batch --exec \
     'select Location from METADATA revs=1;' > locations.$TEST_ID  
  
  # kill rs1; rs2 will be killed by the error inducer
  stop_rs1
  
  # wait for recovery to complete 
  wait_for_recovery "rs1"
  wait_for_recovery "rs2"
  
  # dump keys
  $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
      | grep -v "hypertable" > dbdump.$TEST_ID
  if [ $? != 0 ] ; then
    echo "Problem dumping table 'failover-test', exiting ..."
    exit 1
  fi
  
  # shut everything down
  stop_rs2
  stop_rs3
  $HT_HOME/bin/stop-servers.sh

  $DIGEST < dbdump.$TEST_ID > dbdump.md5
  diff data.md5 dbdump.md5 > out
  if [ $? != 0 ] ; then
    echo "Test $TEST_ID FAILED." >> report.txt
    echo "Test $TEST_ID FAILED." >> errors.txt
    cat out >> report.txt
    touch error
    $HT_SHELL -l error --batch < $SCRIPT_DIR/dump-test-table.hql \
        | grep -v "hypertable" > dbdump.$TEST_ID.again
    if [ $? != 0 ] ; then
        echo "Problem dumping table 'failover-test', exiting ..."
        exit 1
    fi
  else
    echo "Test $TEST_ID PASSED." >> report.txt
  fi
}

if [ "$TEST_0" ] || [ "$TEST_1" ] ; then
    rm -f errors.txt
fi

rm -f report.txt

gen_test_data

env | grep '^TEST_[1-9][0-9]\?=' || set_tests 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 32 33 34 35 36 37 38 39 40

[ "$TEST_1" ] && run_test1 1 "--induce-failure=recover-server-ranges-root-1:exit:0"
[ "$TEST_2" ] && run_test1 2 "--induce-failure=recover-server-ranges-root-4.1:exit:0"
[ "$TEST_3" ] && run_test1 3 "--induce-failure=recover-server-ranges-root-7:exit:0"
[ "$TEST_4" ] && run_test1 4 "--induce-failure=recover-server-ranges-root-10:exit:0"
[ "$TEST_5" ] && run_test1 5 "--induce-failure=recover-server-ranges-root-13:exit:0"
[ "$TEST_6" ] && run_test1 6 "--induce-failure=recover-server-ranges-metadata-1:exit:0"
[ "$TEST_7" ] && run_test1 7 "--induce-failure=recover-server-ranges-metadata-4.1:exit:0"
[ "$TEST_8" ] && run_test1 8 "--induce-failure=recover-server-ranges-metadata-7:exit:0"
[ "$TEST_9" ] && run_test1 9 "--induce-failure=recover-server-ranges-metadata-10:exit:0"
[ "$TEST_10" ] && run_test1 10 "--induce-failure=recover-server-ranges-metadata-13:exit:0"
[ "$TEST_11" ] && run_test1 11 "--induce-failure=recover-server-ranges-user-1:exit:0"
[ "$TEST_12" ] && run_test1 12 "--induce-failure=recover-server-ranges-user-4.1:exit:0"
[ "$TEST_13" ] && run_test1 13 "--induce-failure=recover-server-ranges-user-7:exit:0"
[ "$TEST_14" ] && run_test1 14 "--induce-failure=recover-server-ranges-user-10:exit:0"
[ "$TEST_15" ] && run_test1 15 "--induce-failure=recover-server-ranges-user-13:exit:0"
[ "$TEST_16" ] && run_test1 16 "--induce-failure=recover-server-1:exit:0"
[ "$TEST_17" ] && run_test1 17 "--induce-failure=recover-server-2:exit:0"
[ "$TEST_18" ] && run_test1 18 "--induce-failure=recover-server-3:exit:0"
[ "$TEST_19" ] && run_test1 19 "--induce-failure=recover-server-4:exit:0"
[ "$TEST_20" ] && run_test1 20 "--induce-failure=recover-server-5:exit:0"
[ "$TEST_21" ] && run_test1 21 "--induce-failure=recover-server-6:exit:0"
[ "$TEST_22" ] && run_test1 22 "--induce-failure=recover-server-ranges-root-replay-commit-logs:throw"
[ "$TEST_23" ] && run_test1 23 "--induce-failure=recover-server-ranges-user-12:exit:0;recover-server-ranges-user-14:throw" 

[ "$TEST_24" ] && run_test2 24 "--induce-failure=recover-server-ranges-user-3.1:exit:0" "--induce-failure=replay-fragments-1:exit:0"
[ "$TEST_25" ] && run_test2 25 "--induce-failure=recover-server-ranges-user-3.1:exit:0" "--induce-failure=phantom-receive-1:exit:0"
[ "$TEST_26" ] && run_test2 26 "--induce-failure=recover-server-ranges-user-3.1:exit:0" "--induce-failure=phantom-receive-2:exit:0"
[ "$TEST_27" ] && run_test2 27 "--induce-failure=recover-server-ranges-root-6:exit:0" "--induce-failure=phantom-prepare-ranges-1:exit:0"
[ "$TEST_28" ] && run_test2 28 "--induce-failure=recover-server-ranges-user-6:exit:0" "--induce-failure=phantom-prepare-ranges-1:exit:0"
[ "$TEST_29" ] && run_test2 29 "--induce-failure=recover-server-ranges-user-9:exit:0" "--induce-failure=phantom-commit-1:exit:0"

[ "$TEST_30" ] && run_test3 30 "--induce-failure=replay-fragments-1:exit:0"
[ "$TEST_31" ] && run_test3 31 "--induce-failure=replay-fragments-2:exit:0"
[ "$TEST_32" ] && run_test3 32 "--induce-failure=phantom-receive-1:exit:0"
[ "$TEST_33" ] && run_test3 33 "--induce-failure=phantom-prepare-ranges-1:exit:0"
[ "$TEST_34" ] && run_test3 34 "--induce-failure=phantom-prepare-ranges-2:exit:0"
[ "$TEST_35" ] && run_test3 35 "--induce-failure=phantom-prepare-ranges-3:exit:0"
[ "$TEST_36" ] && run_test3 36 "--induce-failure=phantom-prepare-ranges-4:exit:0"
[ "$TEST_37" ] && run_test3 37 "--induce-failure=phantom-commit-1:exit:0"
[ "$TEST_38" ] && run_test3 38 "--induce-failure=phantom-commit-2:exit:0"
[ "$TEST_39" ] && run_test3 39 "--induce-failure=phantom-commit-3:exit:0"
[ "$TEST_40" ] && run_test3 40 "--induce-failure=phantom-commit-4:exit:0"
[ "$TEST_41" ] && run_test3 41 "--induce-failure=phantom-commit-5:exit:0"
[ "$TEST_42" ] && run_test3 42 "--induce-failure=phantom-commit-6:exit:0"

[ "$TEST_43" ] && run_test4 43 "--induce-failure=recover-server-ranges-root-4.1:exit:0"
[ "$TEST_44" ] && run_test4 44 "--induce-failure=recover-server-ranges-user-4.1:exit:0"
[ "$TEST_45" ] && run_test4 45 "--induce-failure=recover-server-ranges-user-7:exit:0"
[ "$TEST_46" ] && run_test4 46 "--induce-failure=recover-server-ranges-user-10:exit:0"

[ "$TEST_47" ] && run_test2 47 "--induce-failure=recover-server-user-3:exit:0" "--induce-failure=phantom-receive-1:exit:0"
[ "$TEST_48" ] && run_test2 48 "--induce-failure=recover-server-user-4:exit:0" "--induce-failure=phantom-receive-1:exit:0"
[ "$TEST_49" ] && run_test2 49 "--induce-failure=recover-server-user-5:exit:0" "--induce-failure=phantom-receive-1:exit:0"
[ "$TEST_50" ] && run_test2 50 "--induce-failure=recover-server-ranges-user-3.1:exit:0" "--induce-failure=phantom-receive-1:exit:0"
[ "$TEST_51" ] && run_test2 51 "--induce-failure=recover-server-ranges-user-10:exit:0" "--induce-failure=phantom-prepare-ranges-4:exit:0"

[ "$TEST_52" ] && run_test2 52 "--induce-failure=recover-server-ranges-user-3.2:exit:0" "--induce-failure=replay-fragments-1:exit:0"
[ "$TEST_53" ] && run_test2 53 "--induce-failure=recover-server-ranges-user-3.2:exit:0" "--induce-failure=phantom-receive-1:exit:0"
[ "$TEST_54" ] && run_test2 54 "--induce-failure=recover-server-ranges-user-3.2:exit:0" "--induce-failure=phantom-receive-2:exit:0"
[ "$TEST_55" ] && run_test2 55 "--induce-failure=recover-server-ranges-user-3.2:exit:0" "--induce-failure=phantom-receive-1:exit:0"
[ "$TEST_56" ] && run_test1 56 "--induce-failure=recover-server-ranges-root-4.2:exit:0"
[ "$TEST_57" ] && run_test4 57 "--induce-failure=recover-server-ranges-root-4.2:exit:0"
[ "$TEST_58" ] && run_test4 58 "--induce-failure=recover-server-ranges-user-4.2:exit:0"
[ "$TEST_59" ] && run_test1 59 "--induce-failure=recover-server-ranges-user-4.2:exit:0"
[ "$TEST_60" ] && run_test1 60 "--induce-failure=recover-server-ranges-metadata-4.2:exit:0"

if [ -e errors.txt ] && [ "$TEST_21" ] ; then
    ARCHIVE_DIR="archive-"`date | sed 's/ /-/g'`
    mkdir $ARCHIVE_DIR
    mv core.* dbdump.* rangeserver.output.* errors.txt $ARCHIVE_DIR
fi

echo ""
echo "**** TEST REPORT ****"
echo ""
cat report.txt
grep FAILED report.txt > /dev/null && exit 1
exit 0
