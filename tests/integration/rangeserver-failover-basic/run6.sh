#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL=$HT_HOME/bin/hypertable
SCRIPT_DIR=`dirname $0`
MAX_KEYS=${MAX_KEYS:-"200000"}
RS1_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs2.pid
MS2_PIDFILE=$HT_HOME/run/Hypertable.Master.2.pid
RUN_DIR=`pwd`

. $HT_HOME/bin/ht-env.sh

# get rid of all old logfiles
\rm -rf $HT_HOME/log/*

wait_for_recovery() {
  grep "Leaving RecoverServer rs1 state=COMPLETE" master.2.output
  while [ $? -ne "0" ]
  do
    sleep 2
    grep "Leaving RecoverServer rs1 state=COMPLETE" master.2.output
  done
}

stop_rs1() {
  echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38060
  sleep 5
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs1.pid`
}

stop_rs2() {
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs2.pid`
}

stop_master() {
  kill -9 `cat $HT_HOME/run/Hypertable.Master.pid`
}

# stop and start servers
rm keys.* 
$HT_HOME/bin/start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --clear --config=${SCRIPT_DIR}/test.cfg
# start both rangeservers
#$HT_HOME/bin/ht valgrind -v --log-file=vg_rs1.log --track-origins=yes \
#   $HT_HOME/bin/Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=38060 --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs1.output&
sleep 10
$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=38061 --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs2.output&
# start a standby-master
$HT_HOME/bin/ht Hypertable.Master --verbose --pidfile=$MS2_PIDFILE \
   --port=37011 --config=${SCRIPT_DIR}/test.cfg 2>1 > master.2.output&

# create table
$HT_HOME/bin/ht shell --no-prompt < $SCRIPT_DIR/create-table.hql

# write data 
$HT_HOME/bin/ht ht_load_generator update \
    --Hypertable.Mutator.FlushDelay=50 \
    --rowkey.component.0.order=random \
    --rowkey.component.0.type=integer \
    --rowkey.component.0.format="%020lld" \
    --rowkey.component.0.min=0 \
    --rowkey.component.0.max=10000000000 \
    --row-seed=1 \
    --Field.value.size=200 \
    --max-keys=$MAX_KEYS

# dump keys
${HT_HOME}/bin/ht shell --config=${SCRIPT_DIR}/test.cfg --no-prompt --exec "use '/'; select * from LoadTest KEYS_ONLY into file '${RUN_DIR}/keys.pre';"

# kill rs1 and the master
stop_rs1
stop_master

# wait for recovery to complete 
wait_for_recovery

# dump keys
${HT_HOME}/bin/ht shell --config=${SCRIPT_DIR}/test.cfg --no-prompt --exec "use '/'; select * from LoadTest KEYS_ONLY into file '${RUN_DIR}/keys.post';"

# bounce servers
$HT_HOME/bin/stop-servers.sh
stop_rs2
killall Hypertable.Master

# start master and rs2
$HT_HOME/bin/start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --config=${SCRIPT_DIR}/test.cfg
$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=38061 --config=${SCRIPT_DIR}/test.cfg 2>1 >> rangeserver.rs2.output&

# dump keys
${HT_HOME}/bin/ht shell --config=${SCRIPT_DIR}/test.cfg --no-prompt --exec "use '/'; select * from LoadTest KEYS_ONLY into file '${RUN_DIR}/keys.post2';"

# stop servers
$HT_HOME/bin/stop-servers.sh
stop_rs2

# check output
TOTAL_KEYS=`cat keys.post|wc -l`
TOTAL_KEYS2=`cat keys.post2|wc -l`
EXPECTED_KEYS=`cat keys.pre|wc -l`
echo "Total keys returned=${TOTAL_KEYS}, ${TOTAL_KEYS2}, expected keys=${EXPECTED_KEYS}"

if [ "$TOTAL_KEYS" -ne "$EXPECTED_KEYS" ]
then
  echo "Test failed, expected ${EXPECTED_KEYS}, retrieved ${TOTAL_KEYS}"
  exit 1
fi

if [ "$TOTAL_KEYS_2" -ne "$EXPECTED_KEYS" ]
then
  echo "(2) Test failed, expected ${EXPECTED_KEYS}, retrieved ${TOTAL_KEYS2}"
  exit 1
fi

echo "Test passed"

exit 0
