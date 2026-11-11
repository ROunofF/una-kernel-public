/**
 ******************************************************************************
 * @file    SensorMagnetometer.hpp
 * @brief   Magnetometer SensorLayer binding.
 *
 *          Thin binding: it owns no hardware and buffers nothing. The
 *          magnetometer External component runs the worker thread and pushes
 *          each sample here; this class hands it to the raw sub-sensor and
 *          drives the component's rate and power from what is asked of it.
 *
 *          It is the part's only listener: the component offers a single
 *          callback slot, so a second registration would displace the first.
 *          Everything else subscribes to the published channels.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SENSOR_MAGNETOMETER_HPP
#define SENSOR_MAGNETOMETER_HPP

#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Sensors/Magnetometer/MagSubSensorField.hpp"
#include "SensorLayer/Sensors/Magnetometer/MagSubSensorFieldRaw.hpp"
#include "IMagnetometer.hpp"

#include <cstdint>

namespace Sensor
{
    class Magnetometer : public Sensor::ISensorDriverCtrl,
                         public Sensor::MagSubSensorFieldRaw::DataProvider,
                         public Interface::IMagnetometer::DataCallback
    {
    public:
        Magnetometer();

        /**
         * @brief   Bring the part up and take it over.
         * @note    The part arrives here rather than at construction because
         *          which magnetometer exists is only settled once the hardware
         *          scan has run.
         * @param   magnetometer: The part.
         * @param   calibration:  The correction the corrected channel applies.
         * @retval  'true' if the part came up. Nothing else here may be used
         *          if it did not.
         */
        bool init(Interface::IMagnetometer&  magnetometer,
                  Interface::IMagCorrection& calibration);

        Sensor::Driver& getDriverField();
        Sensor::Driver& getDriverFieldRaw();

        //// ISensorDriverCtrl
        float       sdcStart(Sensor::Driver* driver, float period)        override;
        void        sdcStop(Sensor::Driver* driver)                       override;
        float       sdcUpdatePeriod(Sensor::Driver* driver, float period) override;
        float       sdcGetMinPeriod(Sensor::Driver* driver)               override;
        const char* sdcGetDescription(Sensor::Driver* driver)             override;

    private:
        /// Which sub-sensor a driver belongs to.
        Sensor::ISubSensor* getSubSensor(Sensor::Driver* driver);

        //// MagSubSensorFieldRaw::DataProvider
        bool  requestRawFieldPeriod(uint32_t value) override;
        float getMinRawFieldPeriod() override;

        //// Interface::IMagnetometer::DataCallback - the field as measured,
        //// invoked from the component's worker thread.
        void onMagnetic(uint64_t timestampUs, float x, float y, float z,
                        uint32_t periodUs) override;

        /// Null until init() has succeeded; no driver is registered before.
        Interface::IMagnetometer* mpMagnetometer = nullptr;

        /// Declared in build order: the corrected channel subscribes to the
        /// raw one.
        Sensor::MagSubSensorFieldRaw mSubSensorFieldRaw;
        Sensor::MagSubSensorField    mSubSensorField;
    }; /* class Magnetometer */

} // namespace Sensor

#endif // SENSOR_MAGNETOMETER_HPP
