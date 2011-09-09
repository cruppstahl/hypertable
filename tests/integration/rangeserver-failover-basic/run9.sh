#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
HYPERTABLE_HOME=${HT_HOME}
HT_SHELL=$HT_HOME/bin/hypertable
SCRIPT_DIR=`dirname $0`
MAX_KEYS=${MAX_KEYS:-"200000"}
RS1_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs1.pid
RS2_PIDFILE=$HT_HOME/run/Hypertable.RangeServer.rs2.pid
MASTER_LOG=$HT_HOME/log/Hypertable.Master.log
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
}

stop_rs1() {
  echo "shutdown; quit;" | $HT_HOME/bin/ht rsclient localhost:38060
  sleep 5
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs1.pid`
}

stop_rs2() {
  kill -9 `cat $HT_HOME/run/Hypertable.RangeServer.rs2.pid`
}

\rm /tmp/failover-run9-output

# install the sample hook
cp ${HYPERTABLE_HOME}/conf/notification-hook.sh notification-hook.bak
cp ${SCRIPT_DIR}/run9-notification-hook.sh ${HYPERTABLE_HOME}/conf/notification-hook.sh

# stop and start servers
$HT_HOME/bin/start-test-servers.sh --no-rangeserver --no-thriftbroker \
    --clear --induce-failure=bad-log-fragments-1:signal:0 \
    --config=${SCRIPT_DIR}/test.cfg
# start both rangeservers
$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS1_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs1 \
   --Hypertable.RangeServer.Port=38060 --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs1.output&
sleep 10
$HT_HOME/bin/ht Hypertable.RangeServer --verbose --pidfile=$RS2_PIDFILE \
   --Hypertable.RangeServer.ProxyName=rs2 \
   --Hypertable.RangeServer.Port=38061 --config=${SCRIPT_DIR}/test.cfg 2>1 > rangeserver.rs2.output&

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

sleep 5

# kill rs1
stop_rs1

# wait for failure recovery
wait_for_recovery

# stop servers
killall Hypertable.Master
killall Hyperspace.Master
stop_rs2

# check if the hook was executed
sed "s/HOSTNAME/`hostname`/" ${SCRIPT_DIR}/failover-run9-golden > golden
diff /tmp/failover-run9-output golden

if [ "$?" -ne "0" ]
then
  echo "Test failed, golden file differs"
  # restore the old hook
  mv notification-hook.bak ${HYPERTABLE_HOME}/conf/notification-hook.sh
  exit 1
fi

# restore the old hook
mv notification-hook.bak ${HYPERTABLE_HOME}/conf/notification-hook.sh

echo "Test passed"
exit 0

