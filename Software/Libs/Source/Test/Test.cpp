#include "Test/Test.hpp"

//#include "Test/TestFsRace.hpp"
//#include "Test/TestHikingSensors.hpp"
//#include "Test/TestDHSensors.hpp"
//#include "Test/TestAirohaGPS.hpp"
//#include "Test/TestIMU.hpp"
//#include "Test/TestBMI270.hpp"
//#include "Test/TestSensorIMU.hpp"
//#include "Test/TestSensorXL.hpp"
//#include "Test/TestSensorRate.hpp"
//#include "Test/TestSensorSourceStep.hpp"
//#include "Test/TestSensorJoinLeave.hpp"
//#include "Test/TestSensorMagnetometer.hpp"
//#include "Test/TestBarometer.hpp"
//#include "Test/TestHRMonSensors.hpp"
//#include "Test/TestRestartApp.hpp"
//#include "Test/TestSensorFloor.hpp"
//#include "Test/TestSensorAltimeter.hpp"
//#include "Test/TestSensorPressure.hpp"
//#include "Test/TestSensorBattery.hpp"
//#include "Test/TestAppQueue.hpp"
//#include "Test/TestAppQueue_Extended.hpp"
//#include "Test/TestAppApplication.hpp"
//#include "Test/TestAppManager.hpp"
//#include "Test/TestSensorLayerMessage.hpp"
//#include "Test/TestSensorLayerEventBased.hpp"
//#include "Test/TestPAH8316LS.hpp"
//#include "Test/TestBodySensor.hpp"
//#include "Test/TestWorkerPool.hpp"
//#include "Test/TestSleepWakeup.hpp"
//#include "Test/TestSleepTiming.hpp"
//#include "Test/TestMD5.hpp"
//#include "Test/TestDeviceID.hpp"
//#include "Test/TestEMMC.hpp"
//#include "Test/DailyHealth/TestHealthCalculator.hpp"
//#include "Test/DailyHealth/TestDailyLedger.hpp"
//#include "Test/Activity/TestActivityTracker.hpp"
//#include "Test/Activity/TestFeatureCenter.hpp"
//#include "Test/Activity/TestFsm.hpp"
//#include "Test/Activity/TestActivitySensors.hpp"

void Test_execute(void)
{
#ifdef TEST_FS_RACE_HPP
    TestFsRace_execute();
#endif

#ifdef __TEST_HIKING_SENSORS_HPP
    TestHikingSensors_execute();
#endif

#ifdef __TEST_DH_SENSORS_HPP
    TestDHSensors_execute();
#endif

#ifdef __TEST_AIROHA_GPS_HPP
    TestAirohaGPS_execute();
#endif

#ifdef __TEST_IMU_HPP
    TestIMU_execute();
#endif

#ifdef TEST_BMI270_HPP
    TestBMI270_execute();
#endif

#ifdef __TEST_SENSOR_IMU_HPP
    TestSensorIMU_execute();
#endif

#ifdef __TEST_SENSOR_XL_HPP
    TestSensorXL_execute();
#endif

#ifdef TEST_SENSOR_RATE_HPP
    TestSensorRate_execute();
#endif

#ifdef TEST_SENSOR_SOURCE_STEP_HPP
    TestSensorSourceStep_execute();
#endif

#ifdef TEST_SENSOR_JOIN_LEAVE_HPP
    TestSensorJoinLeave_execute();
#endif

#ifdef TEST_SENSOR_MAGNETOMETER_HPP
    TestSensorMagnetometer_execute();
#endif

#ifdef TEST_BAROMETER_HPP
    TestBarometer_execute();
#endif

#ifdef __TEST_HRMON_SENSORS_HPP
    TestHRMonSensors_execute();
#endif

#ifdef __TEST_RESTART_APP_HPP
    TestRestartApp_execute();
#endif

#ifdef __TEST_SENSOR_FLOOR_HPP
    TestSensorFloor_execute();
#endif

#ifdef __TEST_SENSOR_ALTIMETER_HPP
    TestSensorAltimeter_execute();
#endif

#ifdef __TEST_SENSOR_PRESSURE_HPP
    TestSensorPressure_execute();
#endif

#ifdef __TEST_SENSOR_BATTERY_HPP
    TestSensorBattery_execute();
#endif

#ifdef __TEST_APP_QUQUE_HPP
    TestAppQueue_execute();
#endif

#ifdef __TEST_APP_QUQUE_EXTENDED_HPP
    TestAppQueue_Extended_execute();
#endif

#ifdef __TEST_APP_APPLICATION_HPP
    TestAppApplication_execute();
#endif

#ifdef __TEST_APP_MANAGER_HPP
    TestAppManager_execute();
#endif

#ifdef __TEST_SENSOR_LAYER_MESSAGE_HPP
    TestSensorLayerMessage_execute();
#endif

#ifdef __TEST_SENSOR_LAYER_EVENT_BASED_HPP
    TestSensorLayerEventBased_execute();
#endif

#ifdef __TEST_PAH8316LS_HPP
    TestPAH8316LS_execute();
#endif

#ifdef __TEST_BODY_SENSOR_HPP
    TestBodySensor_execute();
#endif

#ifdef TEST_SLEEP_WAKEUP_HPP
    TestSleepWakeup_execute();
#endif

#ifdef TEST_SLEEP_TIMING_HPP
    TestSleepTiming_execute();
#endif

#ifdef __TEST_WORKER_POOL_HPP
    TestWorkerPool_execute();
#endif

#ifdef __TestMD5_h__
    TestMD5_execute();
#endif

#ifdef __TestDeviceID_h__
    TestDeviceID_execute();
#endif

#ifdef __TEST_EMMC_HPP
    TestEMMC_execute();
#endif

#ifdef TEST_DAILY_HEALTH_HEALTH_CALCULATOR_HPP
    TestDailyHealth_HealthCalculator_execute();
#endif

#ifdef TEST_DAILY_HEALTH_DAILY_LEDGER_HPP
    TestDailyHealth_DailyLedger_execute();
#endif

#ifdef TEST_ACTIVITY_ACTIVITY_TRACKER_HPP
    TestActivity_ActivityTracker_execute();
#endif

#ifdef TEST_ACTIVITY_FEATURE_CENTER_HPP
    TestActivity_FeatureCenter_execute();
#endif

#ifdef TEST_ACTIVITY_FSM_HPP
    TestActivity_Fsm_execute();
#endif

#ifdef __TEST_ACTIVITY_SENSORS_HPP
    TestActivitySensors_execute();
#endif
}
