#
# The following 23 tests have the same flow:
#
# 2 RangeServers; 1 crashes, and the Master crashes during recovery via
# the FailureInducer
#
add_test(RangeServer-failover-master-1 env TEST_1=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-2 env TEST_2=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-3 env TEST_3=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-4 env TEST_4=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-5 env TEST_5=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-6 env TEST_6=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-7 env TEST_7=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-8 env TEST_8=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-9 env TEST_9=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-10 env TEST_10=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-11 env TEST_11=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-12 env TEST_12=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-13 env TEST_13=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-14 env TEST_14=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-15 env TEST_15=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-16 env TEST_16=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-17 env TEST_17=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-18 env TEST_18=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-19 env TEST_19=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-20 env TEST_20=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-21 env TEST_21=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-22 env TEST_22=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-23 env TEST_23=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

#
# The following 6 tests have the same flow:
#
# 3 RangeServers; 1 crashes, and the Master crashes during recovery via
# the FailureInducer. Another RangeServer crashes as well, and all 
# ranges are then assigned to the remaining RangeServer.
#
add_test(RangeServer-failover-master-24 env TEST_24=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-25 env TEST_25=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-26 env TEST_26=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-27 env TEST_27=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-28 env TEST_28=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-29 env TEST_29=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

#
# The following 13 tests have the same flow:
#
# 3 RangeServers; 1 crashes to trigger the recovery process, and another
# one crashes via the FailureInducer. All ranges are then assigned to 
# the remaining RangeServer.
#
add_test(RangeServer-failover-master-30 env TEST_30=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-31 env TEST_31=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-32 env TEST_32=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-33 env TEST_33=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-34 env TEST_34=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-35 env TEST_35=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-36 env TEST_36=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-37 env TEST_37=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-38 env TEST_38=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-39 env TEST_39=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-40 env TEST_40=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-41 env TEST_41=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-42 env TEST_42=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

#
# The following 4 tests have the same flow:
#
# 3 RangeServers. During recovery the master crashes. Before the master 
# comes up again, the second range server will go down and force the master 
# to re-create the recovery plan.
#
add_test(RangeServer-failover-master-43 env TEST_43=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-44 env TEST_44=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-45 env TEST_45=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-46 env TEST_46=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

#
# The following 5 tests have the same flow:
#
# 3 RangeServers; 1 crashes, and the Master crashes during recovery via
# the FailureInducer. Another RangeServer crashes as well, and all 
# ranges are then assigned to the remaining RangeServer.
#
add_test(RangeServer-failover-master-47 env TEST_47=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-48 env TEST_48=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-49 env TEST_49=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-50 env TEST_50=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-51 env TEST_51=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-52 env TEST_52=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-53 env TEST_53=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-54 env TEST_54=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-55 env TEST_55=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-56 env TEST_56=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-57 env TEST_57=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-58 env TEST_58=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-59 env TEST_59=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

add_test(RangeServer-failover-master-60 env TEST_60=1 DATA_SIZE=200000
         INSTALL_DIR=${INSTALL_DIR}
         bash -x ${CMAKE_CURRENT_SOURCE_DIR}/run.sh)

