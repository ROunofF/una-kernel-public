/**
 ******************************************************************************
 * @file    SensorGrade.cpp
 * @brief   Derived GRADE sensor implementation.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.Grade"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "SensorLayer/Sensors/Grade/SensorGrade.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGrade.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGpsDistance.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "System.hpp"

#include <cmath>

namespace Sensor
{

Grade::Grade()
    : mPressureConnection(SDK::Sensor::Type::PRESSURE, this)
    , mDistanceConnection(SDK::Sensor::Type::GPS_DISTANCE, this)
    , mDriver(*this,
              SDK::Sensor::Type::GRADE,
              SDK::SensorDataParser::Grade::getFieldsNumber(),
              *this)
    , mTick()
{
    mDriver.setSourceTick(&mTick);
    mDataMutex.init(LOG_MODULE_PRX);
}

Sensor::Driver& Grade::getDriver()
{
    return mDriver;
}

float Grade::sdcStart(Sensor::Driver* /*driver*/, float period)
{
    LOG_INFO("start\n");
    // Reject non-finite periods (NaN/Inf), clamp to the minimum, and cap at the
    // uint32_t-safe maximum before the cast for mTick.start() (an out-of-range
    // float->uint32_t cast is undefined behaviour).
    float safePeriod =
        (std::isfinite(period) && period >= kMinPeriodMs) ? period : kMinPeriodMs;
    if (safePeriod > kMaxPeriodMs) {
        safePeriod = kMaxPeriodMs;
    }
    bool expected = false;

    if (mActive.compare_exchange_strong(expected, true)) {
        {
            OS::MutexCS cs(mDataMutex);
            mEstimator.reset();
            mHaveAltitude     = false;
            mGpsDistanceValid = false;
            mLatchedAltitudeM = 0.0f;
            mLatchedDistanceM = 0.0f;
        }
        // Both upstream sensors are required; bail cleanly if either fails to
        // connect rather than running a timer that can never produce a grade.
        if (!mPressureConnection.connect(kInputPeriodMs) ||
            !mDistanceConnection.connect(kInputPeriodMs)) {
            LOG_WARNING("grade: upstream connect failed\n");
            mPressureConnection.disconnect();
            mDistanceConnection.disconnect();
            mActive.store(false);
            return 0.0f;
        }
        mTick.start(static_cast<uint32_t>(safePeriod));
        mDriver.setSourceStepUs(mTick.stepUs());
    }
    return safePeriod;
}

void Grade::sdcStop(Sensor::Driver* /*driver*/)
{
    LOG_INFO("stop\n");
    bool expected = true;
    if (mActive.compare_exchange_strong(expected, false)) {
        mPressureConnection.disconnect();
        mDistanceConnection.disconnect();
        mTick.stop();
    }
}

float Grade::sdcUpdatePeriod(Sensor::Driver* /*driver*/, float period)
{
    // Reject non-finite periods (NaN/Inf), clamp sub-minimum, and cap at the
    // uint32_t-safe maximum before the cast for mTick.start() - mirrors the
    // guard in sdcStart() (an out-of-range float->uint32_t cast is UB).
    if (!std::isfinite(period) || period < kMinPeriodMs) {
        period = kMinPeriodMs;
    } else if (period > kMaxPeriodMs) {
        period = kMaxPeriodMs;
    }
    if (mActive.load()) {
        mTick.start(static_cast<uint32_t>(period));
        mDriver.setSourceStepUs(mTick.stepUs());
    }
    return period;
}

float Grade::sdcGetMinPeriod(Sensor::Driver* /*driver*/)
{
    return kMinPeriodMs;
}

const char* Grade::sdcGetDescription(Sensor::Driver* /*driver*/)
{
    return "Terrain grade";
}

void Grade::sdcNewConnection(Sensor::Driver* /*driver*/)
{
}

void Grade::onSdlNewData(uint16_t                 handle,
                         const SDK::Sensor::Data* base,
                         uint16_t                 count,
                         uint16_t                 stride)
{
    if (!mActive.load()) {
        return;
    }

    if (base == nullptr || count == 0u) {
        return;  // guard a null/empty callback before constructing/indexing the batch
    }

    SDK::Sensor::DataBatch batch(base, count, stride);

    OS::MutexCS cs(mDataMutex);

    // Use the freshest sample in the batch (the latch only needs the latest
    // value), not the oldest, when count > 1.
    const uint16_t lastIdx = static_cast<uint16_t>(batch.size() - 1);

    if (mPressureConnection.matchesDriver(handle)) {
        SDK::SensorDataParser::Pressure parser(batch[lastIdx]);
        bool good = false;
        if (parser.isDataValid()) {
            // Relative altitude from a fixed reference; only the 10-s delta is
            // used, so the reference cancels (QNH-independent).
            const float alt = SDK::SensorDataParser::Pressure::getAltitude(
                parser.getPressure(), kReferencePressurePa);
            if (std::isfinite(alt)) {
                mLatchedAltitudeM = alt;
                mAltitudeAtMs     = parser.getTimestamp();
                good              = true;
            }
        }
        // The latest pressure sample's validity wins: a good sample arms the
        // window advance; an invalid/NaN one flags a glitch so the estimator
        // fully reacquires instead of silently surviving the bad tick.
        mHaveAltitude   = good;
        mPressureGlitch = !good;
    } else if (mDistanceConnection.matchesDriver(handle)) {
        SDK::SensorDataParser::GpsDistance parser(batch[lastIdx]);
        if (parser.isDataValid()) {
            mLatchedDistanceM = parser.getDistance();
            mGpsDistanceValid = true;
        } else {
            mGpsDistanceValid = false;
        }
    }
}

void Grade::sensorRefresh()
{
    if (!mActive.load()) {
        return;
    }

    if (!mDriver.tickDue()) {
        return;
    }

    const uint32_t nowMs = ::Driver::System::GetTimeMs();
    GradeEstimator::Sample sample;
    {
        OS::MutexCS cs(mDataMutex);
        // The pressure is measured on a clock of its own, so a tick can come
        // just ahead of the sample of its second: it then takes the one
        // before. One older than that is not taken. The barometer has
        // stopped delivering, and a window stepped with one altitude over
        // a growing distance would read as a level road.
        const bool haveAltitude = mHaveAltitude
                && ((nowMs - mAltitudeAtMs) <= kAltitudeMaxAgeMs);

        if (haveAltitude) {
            // Advance the window with the latest barometric sample.
            mEstimator.update(mLatchedAltitudeM, mLatchedDistanceM,
                              mGpsDistanceValid);
            sample = mEstimator.getSample();
        } else if (mPressureGlitch) {
            // An invalid/NaN baro sample arrived: drop the whole window so grade
            // must fully reacquire (mirrors the GPS-loss path), rather than
            // surviving the glitch and re-validating off one good sample.
            mEstimator.reset();
            sample = mEstimator.getSample();
            sample.gradeValid = false;
        } else {
            // No baro sample on this stream yet, or none recent enough: hold
            // the window and report grade as not currently valid until one
            // arrives.
            sample = mEstimator.getSample();
            sample.gradeValid = false;
        }
        // A GPS distance must arrive every tick to be used again.
        mPressureGlitch   = false;
        mGpsDistanceValid = false;
    }
    publishSample(nowMs, sample);
}

void Grade::publishSample(uint32_t nowMs, const GradeEstimator::Sample &sample)
{
    auto& s = mDriver.getDataSample();
    s.setTimestamp(nowMs);
    s.f[SDK::SensorDataParser::Grade::Field::GRADE_PCT]   = sample.gradePct;
    s.u[SDK::SensorDataParser::Grade::Field::GRADE_VALID] = sample.gradeValid ? 1u : 0u;
    mDriver.pushDataSample();
}

} // namespace Sensor
