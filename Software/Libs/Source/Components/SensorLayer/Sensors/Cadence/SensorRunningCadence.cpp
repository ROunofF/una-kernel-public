/**
 ******************************************************************************
 * @file    SensorRunningCadence.cpp
 * @brief   Periodic RUNNING_CADENCE sensor.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.RunCad"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "SensorLayer/Sensors/Cadence/SensorRunningCadence.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserRunningCadence.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "System.hpp"

namespace Sensor
{

RunningCadence::RunningCadence(Sensor::Driver& stepCounterDriver,
                               Sensor::Driver& stepDetectorDriver)
    : mDriver(*this,
              SDK::Sensor::Type::RUNNING_CADENCE,
              SDK::SensorDataParser::RunningCadence::getFieldsNumber(),
              *this)
    , mStepCounterConnection(&stepCounterDriver, this)
    , mStepDetectorConnection(&stepDetectorDriver, this)
    , mEstimator()
    , mTick()
{
    mDriver.setSourceTick(&mTick);
}

Sensor::Driver& RunningCadence::getDriver()
{
    return mDriver;
}

float RunningCadence::sdcStart(Sensor::Driver* driver, float period)
{
    (void) driver;

    LOG_INFO("start\n");
    const float safePeriod = (period < kMinPeriodMs) ? kMinPeriodMs : period;
    bool expected = false;

    if (mActive.compare_exchange_strong(expected, true)) {
        // Reset before connecting. The step-counter connection's synchronous
        // connect-time sample is intentionally NOT relied upon to seed the
        // window: matchesDriver() rejects it (the connection is not flagged
        // connected until connect() returns), so the window instead seeds on
        // the first post-connect step - well within the ~3 s acquisition span.
        mEstimator.reset();
        if (!mStepCounterConnection.connect()) {
            mActive.store(false);
            return 0.0f;
        }
        // Also hold the step-detector connection: the BMI270 reads the step
        // counter register only inside the shared step interrupt, and the
        // counter's own watermark fires just once per 20 steps (~7 s at
        // 170 SPM > kMaxStepGapUs). The detector keeps that interrupt firing
        // every step so the counter is sampled densely; without it cadence
        // would read INVALID (never wrong) rather than track. Best-effort: a
        // hardware enable failure is not reliably reported through connect() at
        // this layer, so there is no reject/log path for it.
        (void) mStepDetectorConnection.connect();
        mTick.start(static_cast<uint32_t>(safePeriod));
        mDriver.setSourceStepUs(mTick.stepUs());
    }
    return safePeriod;
}

void RunningCadence::sdcStop(Sensor::Driver* driver)
{
    (void) driver;

    LOG_INFO("stop\n");
    bool expected = true;
    if (mActive.compare_exchange_strong(expected, false)) {
        mStepCounterConnection.disconnect();
        mStepDetectorConnection.disconnect();
        mTick.stop();
    }
}

float RunningCadence::sdcUpdatePeriod(Sensor::Driver* driver, float period)
{
    (void) driver;

    if (period < kMinPeriodMs) {
        period = kMinPeriodMs;
    }

    if (mActive.load()) {
        mTick.start(static_cast<uint32_t>(period));
        mDriver.setSourceStepUs(mTick.stepUs());
    }
    return period;
}

float RunningCadence::sdcGetMinPeriod(Sensor::Driver* driver)
{
    (void) driver;
    return kMinPeriodMs;
}

const char* RunningCadence::sdcGetDescription(Sensor::Driver* driver)
{
    (void) driver;
    return "Running cadence";
}

void RunningCadence::sdcNewConnection(Sensor::Driver* driver)
{
    (void) driver;
}

void RunningCadence::onSdlNewData(uint16_t                 handle,
                                  const SDK::Sensor::Data* data,
                                  uint16_t                 count,
                                  uint16_t                 stride)
{
    if (!mActive.load()) {
        return;
    }

    // The step-detector connection exists only to keep per-step interrupts
    // alive (see the header); its samples carry no counter and are ignored.
    if (!mStepCounterConnection.matchesDriver(handle)) {
        return;
    }

    SDK::Sensor::DataBatch batch(data, count, stride);
    for (uint16_t i = 0; i < batch.size(); ++i) {
        const SDK::SensorDataParser::StepCounter sample(batch[i]);
        if (!sample.isDataValid()) {
            continue;
        }

        uint64_t timestampUs = sample.getTimestampUs();
        if (timestampUs == 0u) {
            // Fall back to receipt time if the sample carries no timestamp.
            timestampUs =
                static_cast<uint64_t>(::Driver::System::GetTimeMs()) * 1000ULL;
        }
        mEstimator.onStepCount(timestampUs, sample.getStepCount());
    }
}

void RunningCadence::sensorRefresh()
{
    if (!mActive.load()) {
        return;
    }

    if (!mDriver.tickDue()) {
        return;
    }

    const uint32_t nowMs = ::Driver::System::GetTimeMs();
    const uint64_t nowUs = static_cast<uint64_t>(nowMs) * 1000ULL;
    mEstimator.tickUs(nowUs);
    publishSample(nowMs);
}

void RunningCadence::publishSample(uint32_t nowMs)
{
    const CadenceEstimator::Sample sample = mEstimator.getSample();

    auto& s = mDriver.getDataSample();
    s.setTimestamp(nowMs);
    s.f[SDK::SensorDataParser::RunningCadence::Field::CADENCE_SPM] =
        sample.cadenceSpm;
    s.u[SDK::SensorDataParser::RunningCadence::Field::CADENCE_VALID] =
        sample.cadenceValid ? 1u : 0u;
    mDriver.pushDataSample();
}

} // namespace Sensor
