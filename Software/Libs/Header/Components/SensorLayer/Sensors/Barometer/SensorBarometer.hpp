/**
 ******************************************************************************
 * @file    SensorBarometer.hpp
 * @brief   Barometer SensorLayer binding.
 *
 *          Sits between the barometer component and the sensor layer. The
 *          component pushes each measurement here; this class hands the two
 *          channels to their sub-sensors and drives the component's rate and
 *          power from what is asked of it: the faster of the two channels,
 *          and the sea-level reference calculation while one is running.
 *
 *          It is the part's only listener: the component offers a single
 *          callback slot, so a second registration would displace the first.
 *          Everything else subscribes to the published channels.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SENSOR_BAROMETER_HPP
#define SENSOR_BAROMETER_HPP

#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Sensors/Barometer/BaroSubSensor.hpp"
#include "SensorLayer/Sensors/Barometer/BaroP0Adjuster.hpp"
#include "IBarometer.hpp"
#include "OS.hpp"

#include <cstdint>
#include <limits>

namespace Sensor
{
    class Barometer : public Sensor::ISensorDriverCtrl,
                      public Sensor::BaroSubSensor::DataProvider,
                      public Interface::IBarometer::DataCallback
    {
    public:

        Barometer();

        /**
         * @brief   Bring the part up and take it over.
         * @note    The part arrives here rather than at construction because
         *          which barometer exists is only settled once the hardware
         *          scan has run.
         * @param   barometer: The part.
         * @retval  'true' if the part came up. No driver here may be
         *          registered if it did not.
         */
        bool init(Interface::IBarometer& barometer);

        Sensor::Driver& getDriverTemp();
        Sensor::Driver& getDriverPressure();

        /**
         * @brief   Work out the sea-level reference from a known altitude.
         * @note    Runs the part at its fastest for a short run of samples,
         *          whether or not anything is subscribed, and applies the
         *          result to the pressure published from then on. Does nothing
         *          if the part did not come up.
         * @param   altitude: Altitude above sea level, metres.
         */
        void computeP0(float altitude);

        //// ISensorDriverCtrl
        float       sdcStart(Sensor::Driver* driver, float period)        override;
        void        sdcStop(Sensor::Driver* driver)                       override;
        float       sdcUpdatePeriod(Sensor::Driver* driver, float period) override;
        float       sdcGetMinPeriod(Sensor::Driver* driver)               override;
        const char* sdcGetDescription(Sensor::Driver* driver)             override;

    private:

        /// A period nobody is asking for.
        static constexpr uint32_t skNone = std::numeric_limits<uint32_t>::max();

        /// Which sub-sensor a driver belongs to.
        Sensor::ISubSensor* getSubSensor(Sensor::Driver* driver);

        //// BaroSubSensor::DataProvider
        bool  requestPeriod(BaroSubSensor::Channel channel, uint32_t value) override;
        float getMinPeriod() override;

        //// Interface::IBarometer::DataCallback - invoked from the component's
        //// worker thread.
        void onPressure(uint64_t timestampUs, float mbar,
                        uint32_t periodUs) override;
        void onTemperature(uint64_t timestampUs, float degC,
                           uint32_t periodUs) override;

        /// Ask the part for the shortest period anything here wants, or to
        /// stop if nothing does. Sent on every call: a start or a stop is
        /// harmless to repeat, so no state is kept to decide whether to send.
        /// Called with mMutex held.
        bool apply();

        /// Null until init() has succeeded; no driver is registered before.
        Interface::IBarometer* mpBarometer = nullptr;

        /// Guards what the two channels, the reference calculation and the
        /// part's callback share: the periods asked for, the calculation and
        /// the reference. Requests to the part are only posted under it, so
        /// holding it in the callback cannot wait on the part.
        OS::Mutex mMutex;

        /// The period each channel asks for, ms, by Channel; skNone for a
        /// channel with no consumer.
        uint32_t mPeriodMs[2] = { skNone, skNone };

        Sensor::BaroP0Adjuster mP0Adjuster;

        /// The sea-level reference published with each pressure, Pa.
        float mSeaLevelPa = Sensor::BaroP0Adjuster::skDefaultP0;

        Sensor::BaroSubSensor mSubSensorP;
        Sensor::BaroSubSensor mSubSensorT;

    }; /* class Barometer */

} // namespace Sensor

#endif // SENSOR_BAROMETER_HPP
