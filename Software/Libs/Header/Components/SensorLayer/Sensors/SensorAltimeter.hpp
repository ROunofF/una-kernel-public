/**
 ******************************************************************************
 * @file    SensorAltimeter.hpp
 * @brief   Sensor Altimeter
 *
 *          Publishes the pressure altitude on SDK::Sensor::Type::ALTIMETER.
 *          It has no source of its own: it subscribes to the pressure while a
 *          consumer is connected here, and publishes one altitude for each
 *          pressure sample it is handed, with that sample's instant.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SENSOR_ALTIMETER_HPP
#define SENSOR_ALTIMETER_HPP

#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Core/ISensor.hpp"

#include "SDK/Interfaces/ISensorDataListener.hpp"

#include "Filters/SimpleLPF.hpp"

#include <atomic>
#include <cstdint>

namespace Sensor
{
    class Altimeter : public Interface::ISensor,
                      public Sensor::ISensorDriverCtrl,
                      public SDK::Interface::ISensorDataListener
    {
    public:
        /**
         * @brief   Construct the altimeter on top of the pressure channel.
         * @param   driverPressure: The pressure as measured. Subscribed to
         *          while a consumer is connected here, which is what brings
         *          the part up.
         */
        explicit Altimeter(Sensor::Driver& driverPressure);

        Sensor::Driver& getDriverAltimeter();

        float       sdcStart(Sensor::Driver* driver, float period)        override;
        void        sdcStop(Sensor::Driver* driver)                       override;
        float       sdcUpdatePeriod(Sensor::Driver* driver, float period) override;
        float       sdcGetMinPeriod(Sensor::Driver* driver)               override;
        const char* sdcGetDescription(Sensor::Driver* driver)             override;

        void onSdlNewData(uint16_t                 sensor,
                          const SDK::Sensor::Data* base,
                          uint16_t                 count,
                          uint16_t                 stride) override;

        void sensorRefresh() override;

    private:
        float altitudeISA(float P_pa);

        static constexpr uint16_t mMinPeriod = 100;

        /// Whether a consumer is connected. The period it asked for is
        /// applied by the upstream subscription, not here.
        bool mActive = false;

        /// That period in us, for the pressure channel's delivery thread,
        /// which reads it with the pressure's step to know the step of its
        /// own; relaxed.
        std::atomic<uint32_t> mPeriodUs{0};

        Sensor::Driver& mDriverPressure;
        Sensor::Driver  mDriverAltimeter;

        /// Stepped once per sample published, on the pressure channel's
        /// delivery thread.
        Filter::SimpleLPF mFilter;
    }; /* class Altimeter */

} // namespace Sensor

#endif // SENSOR_ALTIMETER_HPP
