/**
 ******************************************************************************
 * @file    SensorAltimeter.cpp
 * @brief   Sensor altimeter
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "SensorAltimeter"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO

#include "Logger/Logger.h"

#include "SensorLayer/Sensors/SensorAltimeter.hpp"
#include "SensorLayer/Core/SampleRateAdapter.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAltimeter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp"

#include <cmath>

namespace Sensor
{

Altimeter::Altimeter(Sensor::Driver& driverPressure)
    : mDriverPressure(driverPressure)
    , mDriverAltimeter(*this,
                       SDK::Sensor::Type::ALTIMETER,
                       SDK::SensorDataParser::Altimeter::getFieldsNumber(),
                       *this)
    , mFilter(0.3)
{
}

Sensor::Driver& Altimeter::getDriverAltimeter()
{
    return mDriverAltimeter;
}

float Altimeter::sdcStart(Sensor::Driver* driver, float period)
{
    (void) driver;

    LOG_INFO("start\n");

    // The first altitude of a new stream is taken as it is, not smoothed
    // into where the last stream left off.
    mFilter.reset();

    mActive = true;
    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)),
            std::memory_order_relaxed);

    // Subscribing starts the pressure. No latency asked for: one sample per
    // delivery, since batching would only delay an altitude.
    if (!mDriverPressure.connect(this, period, 0)) {
        // Refused, so the layer does not register a consumer nothing feeds.
        mActive = false;
        return 0.0f;
    }

    return period;
}

void Altimeter::sdcStop(Sensor::Driver* driver)
{
    (void) driver;

    LOG_INFO("stop\n");

    mActive = false;

    mDriverPressure.disconnect(this);
}

float Altimeter::sdcUpdatePeriod(Sensor::Driver* driver, float period)
{
    (void) driver;

    // The same subscription, asked again at the new period. Refused, the
    // pressure goes on feeding this sensor at the period it had, so that
    // period stays the one the step of the altitudes is worked out from.
    if (!mDriverPressure.connect(this, period, 0)) {
        return 0.0f;
    }

    mPeriodUs.store(static_cast<uint32_t>(SampleRateAdapter::periodUs(period)),
            std::memory_order_relaxed);

    return period;
}

float Altimeter::sdcGetMinPeriod(Sensor::Driver* driver)
{
    (void) driver;

    return static_cast<float>(mMinPeriod);
}

const char* Altimeter::sdcGetDescription(Sensor::Driver* driver)
{
    (void) driver;

    return "Virtual altimeter";
}

void Altimeter::onSdlNewData(uint16_t                 handle,
                             const SDK::Sensor::Data* base,
                             uint16_t                 count,
                             uint16_t                 stride)
{
    (void) handle;

    // A delivery can still be in flight across a stop.
    if (!mActive) {
        return;
    }

    // What arrives is the pressure as this sensor's own queue on it thinned
    // it, so its step is that thinning's output step.
    mDriverAltimeter.setSourceStepUs(SampleRateAdapter::outputStepUs(
            mPeriodUs.load(std::memory_order_relaxed),
            mDriverPressure.getSourceStepUs()));

    SDK::Sensor::DataBatch batch(base, count, stride);

    for (uint16_t idx = 0; idx < batch.size(); ++idx) {
        const SDK::SensorDataParser::Pressure measured(batch[idx]);

        auto& sample = mDriverAltimeter.getDataSample();

        // When the pressure was measured; every consumer's rate is judged
        // on it.
        sample.setTimestampUs(measured.getTimestampUs());
        sample.f[0] = mFilter.execute(altitudeISA(measured.getPressure()));

        mDriverAltimeter.pushDataSample();

        LOG_DEBUG("alt %f\n", sample.f[0]);
    }
}

void Altimeter::sensorRefresh()
{
    // Nothing to poll: altitudes are published from inside the pressure
    // channel's delivery.
}

/**
 * @brief Compute altitude from pressure using the ISA (International Standard Atmosphere) model.
 *
 * This method estimates the altitude above mean sea level assuming standard
 * atmospheric conditions. Temperature deviations and local pressure variations
 * are not taken into account.
 *
 * Formula:
 * @f[
 * h = 44330 \cdot \left(1 - \left(\frac{P}{P_0}\right)^{\tfrac{1}{5.255877}}\right)
 * @f]
 *
 * @param P_pa Measured static pressure in Pascals.
 * @return Estimated altitude above mean sea level [meters].
 */
float Altimeter::altitudeISA(float P_pa)
{
    static constexpr float P_STD = 101325.0f;       // Sea-level standard atmospheric pressure [Pa]
    static const float     inv_n = 1.0 / 5.255877;

    return 44330.0 * (1.0 - std::pow(P_pa / P_STD, inv_n));
}

} // namespace Sensor
