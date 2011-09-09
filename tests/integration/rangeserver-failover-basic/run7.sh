#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current/"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL=$HT_HOME/bin/hypertable
SCRIPT_DIR=`dirname $0`
MAX_KEYS=${MAX_KEYS:-"500000"}
RS1_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs2.pid
RS3_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs3.pid
MASTER_PIDFILE=$HT_HOME/run/Hypertable.Master.pid
MASTER_LOG=master.output
RUN_DIR=`pwd`

. $HT_HOME/bin/ht-env.sh

# get rid of all old logfiles
\rm -rf $HT_HOME/log/*

wait_for_recovery() {
  grep "Leaving RecoverServer rs1 state=COMPLETE" $MASTER_LOG
  while [ $? -ne "0" ]
  do
    sleep 2
    grep "Leaving RecoverServer rs1 state=COMPLETE" $MASTER_LOG
  done

  grep "Leaving RecoverServer rs2 state=COMPLETE" $MASTER_LOG
  while [ $? -ne "0" ]
  do
    sleep 2
    grep "Leaving RecoverServer rs2 state=COMPLETE" $MASTER_LOG
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

stop_rs3() {
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs3.pid`
}

# stop and start servers
rm metadata.* keys.* rs*dump.* 
rm -rf fs fs_pre
$HT_HOME/bin/start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --no-master --clear --config=${SCRIPT_DIR}/test.cfg
# start the master
$HT_HOME/bin/ht Hypertable.Master --verbose --pidfile=$MASTER_PIDFILE \
   --config=${SCRIPT_DIR}/test.cfg 2>1 > $MASTER_LOG&
# start the rangeservers
$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=38060 --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs1.output&
sleep 10
$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=38061 --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs2.output&
$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS3_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs3 \
   --Hypertable.RangeServer.Port=38062 --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs3.output&

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

# kill rs1
stop_rs1

# after 10 seconds: stop the second RangeServer
stop_rs2

# wait for recovery to complete 
wait_for_recovery

# dump keys again
${HT_HOME}/bin/ht shell --config=${SCRIPT_DIR}/test.cfg --no-prompt --exec "use '/'; select * from LoadTest KEYS_ONLY into file '${RUN_DIR}/keys.post';"

# stop servers
$HT_HOME/bin/stop-servers.sh
stop_rs2
stop_rs3

# check output
TOTAL_KEYS=`cat keys.post|wc -l`
EXPECTED_KEYS=`cat keys.pre|wc -l`
echo "Total keys returned=${TOTAL_KEYS}, expected keys=${EXPECTED_KEYS}"

if [ "$TOTAL_KEYS" -ne "$EXPECTED_KEYS" ]
then
  echo "Test failed, expected ${EXPECTED_KEYS}, retrieved ${TOTAL_KEYS}"
  exit 1
fi

# The "Waiting for grace period" message must be printed 3 times:
#   1. when the grace period of rs1 started
#   2. when the grace period of rs2 started
#   3. when the grace period of rs1 was repeated, because rs2 went down in the
#       meantime
L=`grep "Waiting for grace period..." $MASTER_LOG  | wc -l`
if [ "$L" -ne "3" ]
then
  echo "Test failed"
  exit 1
fi

echo "Test passed"

exit 0
