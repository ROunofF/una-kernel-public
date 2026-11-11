#ifndef INSTANCE_SENSOR_LAYER_HPP
#define INSTANCE_SENSOR_LAYER_HPP

#include "PAH8316LS.hpp"
#include "SensorLayer/Sensors/Barometer/SensorBarometer.hpp"
#include "SensorLayer/Sensors/SensorAltimeter.hpp"
#include "SensorLayer/Sensors/Daily/SensorHeartRateMetricsDaily.hpp"
#include "SensorLayer/Sensors/Gps/SensorGps.hpp"
#include "SensorLayer/Sensors/Floor/SensorFloor.hpp"
#include "SensorLayer/Sensors/Cadence/SensorRunningCadence.hpp"
#include "SensorLayer/Sensors/Grade/SensorGrade.hpp"
#include "SensorLayer/Sensors/SensorBatteryLevel.hpp"
#include "SensorLayer/Sensors/SensorBatteryCharging.hpp"
#include "SensorLayer/Sensors/SensorBatteryMetrics.hpp"
#include "SensorLayer/Sensors/Imu/SensorImu.hpp"
#include "SensorLayer/Sensors/Magnetometer/SensorMagnetometer.hpp"
#include "SensorLayer/Sensors/Body/SensorBody.hpp"
#include "SensorLayer/Sensors/Daily/SensorStepCounterDaily.hpp"
#include "SensorLayer/Sensors/Daily/SensorFloorCounterDaily.hpp"
#include "SensorLayer/Sensors/SensorActivityTime.hpp"
#include "SensorLayer/Sensors/Daily/SensorActivityTimeDaily.hpp"

namespace Instance {
    class SensorLayer
    {
    public:
        static SensorLayer& getInstance();

        void init();

        Sensor::Imu&              getSensorImu();
        Sensor::Floor&            getSensorFloor();
        Sensor::HeartRateMetricsDaily& getSensorHRMetrics();
        Sensor::Barometer&        getSensorBarometer();
        Sensor::Body&             getSensorBody();

        /**
         * @brief The magnetometer binding, which is the part's registered
         *        listener.
         * @note  Registering with the part displaces whoever holds its single
         *        callback slot, so whatever takes it owes it back.
         */
        Sensor::Magnetometer&     getSensorMagnetometer();

    private:
        SensorLayer();
        ~SensorLayer() = default;

        Sensor::Barometer               mSensorBarometer;
        Sensor::Altimeter               mSensorAltimeter;
        Sensor::Gps                     mSensorGps;
        Sensor::BatteryLevel            mSensorBatteryLevel;
        Sensor::BatteryCharging         mSensorBatteryCharging;
        Sensor::BatteryMetrics          mSensorBatteryMetrics;

        Sensor::Imu                     mSensorImu;
        Sensor::Magnetometer            mSensorMagnetometer;

        Sensor::RunningCadence          mSensorRunningCadence;

        Sensor::Floor                   mSensorFloor;
        Sensor::Grade                   mSensorGrade;
        Sensor::HeartRateMetricsDaily   mSensorHRMetrics;

        // Push-fed sink sensors. They register as listeners on
        // DailyHealth::Service and Activity::Service respectively
        // (see init()); construction itself does not depend on those
        // services being ready.
        Sensor::StepCounterDaily        mSensorStepCounterDaily;
        Sensor::FloorCounterDaily       mSensorFloorCounterDaily;
        Sensor::ActivityTime            mSensorActivityTime;
        Sensor::ActivityTimeDaily       mSensorActivityTimeDaily;

        Sensor::Body                    mSensorBody;
    };
}

#endif // INSTANCE_SENSOR_LAYER_HPP
