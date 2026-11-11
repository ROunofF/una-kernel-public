#define LOG_MODULE_PRX      "Inst.Sensor.Layer"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Instances/InstanceSensorLayer.hpp"
#include "Instances/InstanceMagCal.hpp"
#include "Instances/InstanceI2C.hpp"
#include "Instances/InstancePAH8316LS.hpp"
#include "Instances/InstanceActivityService.hpp"
#include "Instances/InstanceDailyHealthService.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "I2c.hpp"
#include "Gpio.hpp"
#include "ComponentFactory.hpp"
#include "FuelGauge.hpp"
#include "SettingsDebugManager.hpp"

namespace Instance {

SensorLayer& SensorLayer::getInstance()
{
    static SensorLayer mInstance;

    return mInstance;
}

void SensorLayer::init()
{
    ComponentFactory& cf = ComponentFactory::GetInstance();
    Sensor::Manager& sm = Sensor::Manager::getInstance();

    // Barometer. The binding brings the part up and takes its callback.
    {
        if (!mSensorBarometer.init(cf.getBarometer())) {
            LOG_WARNING("failed to init barometer chip\n");
        } else {
            sm.regDriver(&mSensorBarometer.getDriverTemp());
            sm.regDriver(&mSensorBarometer.getDriverPressure());
            sm.regDriver(&mSensorAltimeter.getDriverAltimeter());
        }
    }

    // GPS
    {
        Interface::IGps& gps = cf.getGps();
        if (cf.isGpsPresent() && gps.init()) {
            sm.regDriver(&mSensorGps.getDriverLocation());
            sm.regDriver(&mSensorGps.getDriverSpeed());
            sm.regDriver(&mSensorGps.getDriverDistance());
            sm.regDriver(&mSensorGps.getDriverAltimeter());
        } else {
            LOG_WARNING("failed to init GPS chip\n");
        }
    }

    // Magnetometer. The binding brings the part up and takes its callback.
    {
        if (!cf.isMagnetometerPresent()) {
            LOG_INFO("no magnetometer on this board\n");
        } else if (!mSensorMagnetometer.init(cf.getMagnetometer(),
                Instance::MagCal::correction())) {
            LOG_WARNING("failed to init magnetometer chip\n");
        } else {
            sm.regDriver(&mSensorMagnetometer.getDriverField());
            sm.regDriver(&mSensorMagnetometer.getDriverFieldRaw());
        }
    }

    sm.regDriver(&mSensorBatteryLevel.getDriver());
    sm.regDriver(&mSensorBatteryCharging.getDriver());
    sm.regDriver(&mSensorBatteryMetrics.getDriver());

    // The part the sensor was built on, not the factory's answer of now: the
    // sub-sensors registered below drive that one.
    Interface::IImu& imu = mSensorImu.getImu();

    if (!cf.isImuPresent()) {
        LOG_WARNING("IMU is not present\n");
    } else if (imu.init()) {

        // TODO: Set scale same as for the HR
        imu.setAccelRange(8);     // 8G
        imu.setGyroRange(2000);   // 2000DPS

        sm.regDriver(&mSensorImu.getDriverAccel());
        sm.regDriver(&mSensorImu.getDriverAccelRaw());
        sm.regDriver(&mSensorImu.getDriverGyro());
        sm.regDriver(&mSensorImu.getDriverGyroRaw());
        sm.regDriver(&mSensorImu.getDriverFusion());
        sm.regDriver(&mSensorImu.getDriverFusionRaw());
        sm.regDriver(&mSensorImu.getDriverStepCounter());
        sm.regDriver(&mSensorImu.getDriverStepDetector());
        sm.regDriver(&mSensorImu.getDriverMotionDetect());
        sm.regDriver(&mSensorImu.getDriverActivityRecognition());
        sm.regDriver(&mSensorImu.getDriverWristMotion());
    } else {
        LOG_WARNING("failed to init IMU Bosch chip\n");
    }

    if (sm.isDriverRegistered(SDK::Sensor::Type::STEP_DETECTOR) &&
        sm.isDriverRegistered(SDK::Sensor::Type::ALTIMETER)) {
        sm.regDriver(&mSensorFloor.getDriver());
    } else {
        LOG_WARNING("%s sensor is not registered - required sensors are missing\n", "Floor sensor");
    }

    if (sm.isDriverRegistered(SDK::Sensor::Type::STEP_COUNTER)) {
        sm.regDriver(&mSensorRunningCadence.getDriver());
    } else {
        LOG_WARNING("%s sensor is not registered - required sensors are missing\n", "RunningCadence");
    }

    if (sm.isDriverRegistered(SDK::Sensor::Type::PRESSURE) &&
        sm.isDriverRegistered(SDK::Sensor::Type::GPS_DISTANCE)) {
        sm.regDriver(&mSensorGrade.getDriver());
    } else {
        LOG_WARNING("%s sensor is not registered - required sensors are missing\n", "Grade sensor");
    }

    // Move to ComponentFactory
    {
        Hardware::PAH8316LS& pah8316ls = Instance::PAH8316LS::getInstance().getPAH8316LS();

        if (pah8316ls.init()) {
            // TODO: Set scale same as for the accel
            pah8316ls.setAccRange(8); // 8G
            sm.regDriver(&mSensorBody.getDriverHeartRate());
            sm.regDriver(&mSensorBody.getDriverHeartRateEx());
            sm.regDriver(&mSensorBody.getDriverTouch());

            Settings::DebugManager& sdm = Settings::DebugManager::GetInstance();
            pah8316ls.enableDataLogging(sdm.getSettings().ppgCfg.manufLog);
        } else {
            LOG_WARNING("failed to init PAH8316LS chip\n");
        }
    }

    if (sm.isDriverRegistered(SDK::Sensor::Type::HEART_RATE)) {
        sm.regDriver(&mSensorHRMetrics.getDriver());
    } else {
        LOG_WARNING("%s sensor is not registered - required sensors are missing\n", "HeartRateMetricsDaily");
    }

    // ------------------------------------------------------------------
    // Push-fed sink sensors. These have no upstream HW dependencies of
    // their own - they just listen to DailyHealth / Activity and
    // republish onto their driver channels. attachCallback /
    // attachDurationListener on the singletons is safe before those
    // services have been init()'ed; the callbacks start firing once
    // the producers begin running.
    // ------------------------------------------------------------------

    sm.regDriver(&mSensorStepCounterDaily.getDriver());
    sm.regDriver(&mSensorFloorCounterDaily.getDriver());
    sm.regDriver(&mSensorActivityTime.getDriver());
    sm.regDriver(&mSensorActivityTimeDaily.getDriver());

    {
        auto& dhs = Instance::DailyHealthService::getInstance().getService();
        dhs.attachCallback(&mSensorHRMetrics);
        dhs.attachCallback(&mSensorStepCounterDaily);
        dhs.attachCallback(&mSensorFloorCounterDaily);
        dhs.attachCallback(&mSensorActivityTimeDaily);
    }

    {
        auto& act = Instance::ActivityService::getInstance().getService();
        act.attachDurationListener(&mSensorActivityTime);
    }
}

Sensor::Imu& SensorLayer::getSensorImu()
{
    return mSensorImu;
}

Sensor::Floor& SensorLayer::getSensorFloor()
{
    return mSensorFloor;
}

Sensor::HeartRateMetricsDaily& SensorLayer::getSensorHRMetrics()
{
    return mSensorHRMetrics;
}

Sensor::Barometer& SensorLayer::getSensorBarometer()
{
    return mSensorBarometer;
}

Sensor::Magnetometer& SensorLayer::getSensorMagnetometer()
{
    return mSensorMagnetometer;
}

Sensor::Body& SensorLayer::getSensorBody()
{
    return mSensorBody;
}

SensorLayer::SensorLayer()
    : mSensorBarometer()
    , mSensorAltimeter(mSensorBarometer.getDriverPressure())
    , mSensorGps()
    , mSensorBatteryLevel()
    , mSensorBatteryCharging()
    , mSensorBatteryMetrics(FuelGauge::GetInstance())
    // The factory has the IMU only once it has scanned the modules; built
    // before that, this would hold the stub for good.
    , mSensorImu(ComponentFactory::GetInstance().getImu())
    , mSensorRunningCadence(mSensorImu.getDriverStepCounter(),
                            mSensorImu.getDriverStepDetector())
    , mSensorFloor()
    , mSensorGrade()
    , mSensorHRMetrics()
    , mSensorStepCounterDaily()
    , mSensorFloorCounterDaily()
    , mSensorActivityTime()
    , mSensorActivityTimeDaily()
    , mSensorBody(Instance::PAH8316LS::getInstance().getPAH8316LS())
{

}

}
