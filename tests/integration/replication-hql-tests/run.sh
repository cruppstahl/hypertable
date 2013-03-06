#!/usr/bin/env bash

HT_HOME=${INSTALL_DIR:-"/opt/hypertable/current"}
HYPERTABLE_HOME=$HT_HOME
HT_SHELL=$HT_HOME/bin/hypertable
SCRIPT_DIR=`dirname $0`

. $HT_HOME/bin/ht-env.sh

$HT_HOME/bin/start-test-servers.sh --clear --no-thriftbroker \
    --config=$SCRIPT_DIR/hypertable.cfg

sleep 5

cat $SCRIPT_DIR/test.hql | $HT_HOME/bin/ht shell --namespace / --test-mode \
    --config=$SCRIPT_DIR/hypertable.cfg &> test.out

diff test.out $SCRIPT_DIR/test.golden
if [ $? -ne "0" ];
then
  echo "test differs"
  exit 1
fi

exit 0
