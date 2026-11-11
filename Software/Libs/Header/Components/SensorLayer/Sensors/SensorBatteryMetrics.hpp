/**
 ******************************************************************************
 * @file    SensorBatteryMetrics.hpp
 * @brief   Sensor Battery Metrics
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __SENSOR_BATTERY_METRICS_HPP
#define __SENSOR_BATTERY_METRICS_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "FuelGauge.hpp"
#include "SensorLayer/Core/SourceTick.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{
    class BatteryMetrics : public Interface::ISensor,
                           public Sensor::ISensorDriverCtrl
    {
    public:
        BatteryMetrics(FuelGauge& fg);

        Sensor::Driver& getDriver();

        float       sdcStart(Sensor::Driver* driver, float period)        override;
        void        sdcStop(Sensor::Driver* driver)                       override;
        float       sdcUpdatePeriod(Sensor::Driver* driver, float period) override;
        float       sdcGetMinPeriod(Sensor::Driver* driver)               override;
        const char* sdcGetDescription(Sensor::Driver* driver)             override;

        void sensorRefresh() override;

    private:
        void thread();

        struct Flag {
            static constexpr uint32_t ExitRequest  = (1 << 0);
            static constexpr uint32_t ExitResponse = (1 << 1);
        };

        FuelGauge&        mFuelGauge;
        Sensor::Driver    mDriver;
        Sensor::SourceTick mTick;
    }; /* class BatteryMetrics */

} // namespace Sensor

#endif /* __SENSOR_BATTERY_METRICS_HPP */
