/**
 ******************************************************************************
 * @file    SensorBody.cpp
 * @brief   Body sensor
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "SensorBody"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "SensorLayer/Sensors/Body/SensorBody.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "System.hpp"
#include "OS.hpp"
#include "ComponentFactory.hpp"

namespace Sensor
{

Body::Body(Hardware::PAH8316LS& bodySensor)
    : mBodySensor(bodySensor)
    , mHeartRate(*this)
    , mHeartRateEx(*this)
    , mTouch(*this)
    , mArbiter()
    , mHrMutex()
    , mModeMutex()
    , mHrTickTimer()
    , mAccelRaw(SDK::Sensor::Type::ACCELEROMETER_RAW, this,
                1000.0f / kXLRate, kXLLatency)
    , mXLData()
    , mSubSensors{ { &mHeartRate, &mHeartRateEx, &mTouch } }
    , mMode(Body::OperateMode::DISABLE)
{
    mHrMutex.init(LOG_MODULE_PRX);
    mModeMutex.init("BodyMode");
    mXLData.init(LOG_MODULE_PRX);
    mHrTickTimer.init("HrTick", this, &Body::hrTickCallback,
                      /* periodic */ true, nullptr);
}

Sensor::Driver& Body::getDriverHeartRate()
{
    return mHeartRate.getDriver();
}

Sensor::Driver& Body::getDriverHeartRateEx()
{
    return mHeartRateEx.getDriver();
}

Sensor::Driver& Body::getDriverTouch()
{
    return mTouch.getDriver();
}

float Body::sdcStart(Sensor::Driver* driver, float period)
{
    period = getSubSensor(driver)->start(period);

    // Reconfigure the chip unconditionally: an event-based sub-sensor has no
    // period (start() returns 0), which must not be read as a start failure.
    updateBodySensorMode();

    return period;
}

void Body::sdcStop(Sensor::Driver* driver)
{
    getSubSensor(driver)->stop();

    updateBodySensorMode();
}

float Body::sdcUpdatePeriod(Sensor::Driver* driver, float period)
{
    return getSubSensor(driver)->updatePeriod(period);
}

float Body::sdcGetMinPeriod(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getMinPeriod();
}

const char* Body::sdcGetDescription(Sensor::Driver* driver)
{
    return getSubSensor(driver)->getDescription();
}

void Body::sdcNewConnection(Sensor::Driver* driver)
{
    return getSubSensor(driver)->newConnection();
}

uint8_t Body::getActiveCount()
{
    uint8_t summa = 0;

    for (auto s : mSubSensors) {
        if (s->isActive()) {
            ++summa;
        }
    }

    return summa;
}

Sensor::ISubSensor* Body::getSubSensor(Sensor::Driver* driver)
{
    for (auto s : mSubSensors) {
        if (&s->getDriver() == driver) {
            return s;
        }
    }

    assert(false);

    return nullptr;
}

void Body::updateBodySensorMode()
{
    OS::MutexCS lock(mModeMutex);

    // External-only stops wrist-optical HR measurement: HR being consumed no
    // longer forces the PPG on, since the strap is the source. The chip then
    // follows touch demand: if touch is still wanted (the Activity FSM's wear
    // detection) it stays in TOUCH_ONLY — which keeps the LEDs/front-end powered
    // (touch on this part is optical), so that is not a full power-down; if
    // nothing wants touch it goes to DISABLE (LED + sensor off). External & Wrist
    // (mExternalOnly == false) keeps optical on so the fallback is immediate.
    // HR is "consumed" if either the 2-field HEART_RATE or the multi-source
    // HEART_RATE_EX has a subscriber.
    const bool hrConsumed = mHeartRate.isActive() || mHeartRateEx.isActive();
    const bool opticalHrWanted = hrConsumed && !mExternalOnly.load();

    Sensor::Body::OperateMode newMode;
    if (opticalHrWanted) {
        newMode = Body::OperateMode::AUTO;
    } else if (mTouch.isActive()) {
        newMode = Body::OperateMode::TOUCH_ONLY;
    } else {
        newMode = Body::OperateMode::DISABLE;
    }

    // Re-evaluation tick runs whenever HR is consumed, regardless of source —
    // it is the only thing that detects a silent dropout when optical is off.
    if (hrConsumed) {
        if (!mHrTickTimer.isRunning()) {
            mHrTickTimer.start(kHrTickMs);
        }
    } else if (mHrTickTimer.isRunning()) {
        mHrTickTimer.stop();
    }

    if (newMode == mMode) {
        return;
    }

    if (mMode == Body::OperateMode::DISABLE) {
        LOG_INFO("start the body sensor chip\n");
    }

    mMode = newMode;

    switch (mMode) {
    case Body::OperateMode::AUTO:
        LOG_INFO("set AUTO mode\n");
        // Bring the accel feed up before the chip starts requesting windows,
        // so the optical PPG's motion compensation has samples to work with.
        // Drop any stale samples buffered before the previous AUTO exit.
        mXLData.clear();
        mAccelRaw.connect();
        mBodySensor.registerCallback(this);
        mBodySensor.registerAccReqCallback(this);
        mBodySensor.enable(Hardware::PAH8316LS::OperateMode::Auto);
        break;

    case Body::OperateMode::TOUCH_ONLY:
        LOG_INFO("set TOUCH_ONLY mode\n");
        mBodySensor.registerCallback(this);
        mBodySensor.registerAccReqCallback(nullptr);
        mBodySensor.enable(Hardware::PAH8316LS::OperateMode::TouchOnly);
        mAccelRaw.disconnect();   // optical off -> no accel needed
        break;

    case Body::OperateMode::DISABLE:
        LOG_INFO("stop the body sensor chip\n");
        mBodySensor.disable();
        mBodySensor.registerCallback(nullptr);
        mBodySensor.registerAccReqCallback(nullptr);
        mAccelRaw.disconnect();
        break;
    }
}

void Body::onTouch(bool value)
{
    bool changed = false;
    if (value && !mIsTouched.load()) {
        LOG_INFO("Touch detected\n");
        mIsTouched.store(true);
        changed = true;
    } else if (!value && mIsTouched.load()) {
        LOG_INFO("NO Touch detected\n");
        mIsTouched.store(false);
        changed = true;
    }

    mTouch.setTouch(value);

    // Wrist-contact drives the optical gate in publishHr; re-publish on a change
    // so optical HR appears/clears promptly rather than waiting for the next tick.
    if (changed) {
        OS::MutexCS lock(mHrMutex);
        publishHr(::Driver::System::GetTimeMs());
    }
}

void Body::onHeartRate(uint8_t hr, uint8_t trustLevel)
{
    // Optical reading enters the source arbiter; the arbiter decides whether it
    // or a fresh external strap sample feeds the sensor layer.
    OS::MutexCS lock(mHrMutex);
    uint32_t now = ::Driver::System::GetTimeMs();
    mArbiter.submitOptical(hr, trustLevel, now);
    publishHr(now);
}

void Body::submitExternalHr(uint8_t hr, uint8_t trustLevel)
{
    OS::MutexCS lock(mHrMutex);
    uint32_t now = ::Driver::System::GetTimeMs();
    mArbiter.submitExternal(hr, trustLevel, now);
    publishHr(now);
}

void Body::setHrExternalOnly(bool on)
{
    {
        OS::MutexCS lock(mHrMutex);
        mArbiter.setExternalOnly(on);
        publishHr(::Driver::System::GetTimeMs());
    }
    // Power the optical PPG down (external-only) or back up (external & wrist /
    // off). The arbiter already reports invalid HR when the strap is stale, and
    // the tick keeps re-evaluating, so a powered-down optical never strands HR.
    mExternalOnly.store(on);
    updateBodySensorMode();
}

void Body::clearExternalHr()
{
    OS::MutexCS lock(mHrMutex);
    mArbiter.clearExternal();
    publishHr(::Driver::System::GetTimeMs());
}

void Body::hrTickCallback(void * /* arg */)
{
    // Periodic re-evaluation only — no chip access, so this never contends with
    // the mode mutex. Lets the arbiter abandon a stale source between samples.
    OS::MutexCS lock(mHrMutex);
    publishHr(::Driver::System::GetTimeMs());
}

void Body::publishHr(uint32_t nowMs)
{
    const HrSourceArbiter::ExOutput ex = mArbiter.decideEx(nowMs);
    const bool touched = mIsTouched.load();

    // Off-wrist gate, applied centrally here: optical-sourced HR is only valid
    // while worn; an external (chest strap) reading is never wrist-gated.
    uint8_t  arbBpm = 0, arbTrust = 0;
    HrSource arbSrc = HrSource::None;
    if (ex.arbitrated.valid &&
        (touched || ex.arbitrated.source == HrSource::External)) {
        arbBpm   = ex.arbitrated.bpm;
        arbTrust = ex.arbitrated.trust;
        arbSrc   = ex.arbitrated.source;
    }

    const uint8_t optBpm   = (touched && ex.optical.valid) ? ex.optical.bpm   : 0;
    const uint8_t optTrust = (touched && ex.optical.valid) ? ex.optical.trust : 0;
    const uint8_t extBpm   = ex.external.valid ? ex.external.bpm   : 0;
    const uint8_t extTrust = ex.external.valid ? ex.external.trust : 0;

    // 2-field HEART_RATE (arbitrated) for every consumer; 7-field HEART_RATE_EX
    // (arbitrated + source + raw optical + raw external) for opt-in consumers.
    mHeartRate.setData(arbBpm, arbTrust);
    mHeartRateEx.setData(arbBpm, arbTrust, arbSrc, optBpm, optTrust, extBpm, extTrust);
}

void Body::onSdlNewData(uint16_t                 handle,
                        const SDK::Sensor::Data* base,
                        uint16_t                 count,
                        uint16_t                 stride)
{
    if (!mAccelRaw.matchesDriver(handle)) {
        return;
    }

    SDK::Sensor::DataBatch batch(base, count, stride);
    if (batch.size() == 0) {
        return;   // nothing to buffer; also guards the batch[] reads in the log below
    }

    for (uint16_t idx = 0; idx < batch.size(); ++idx) {
        SDK::SensorDataParser::AccelerometerRaw parser(batch[idx]);
        xl d;
        d.ts = parser.getTimestamp();
        parser.getXYZ(d.x, d.y, d.z);

        // Drop the oldest sample if the queue is full.
        if (mXLData.available() == 0) {
            xl dummy {};
            mXLData.pop(dummy, 0);
        }
        mXLData.push(d, 0);
    }

    LOG_DEBUG("onSdlNewData: batch=%u mXLData=%ld, ts %u-%u, lag=%d\n",
              batch.size(),
              mXLData.size(),
              batch[0].getTimestamp(),
              batch[batch.size() - 1].getTimestamp(),
              (int32_t)(::Driver::System::GetTimeMs() - batch[batch.size() - 1].getTimestamp()));
}

void Body::onAccRequest(uint32_t                startTimestamp,
                        uint32_t                endTimestamp,
                        uint32_t                minSamples,
                        uint32_t                maxSamples,
                        IBodySensor::AccSample* buff,
                        uint32_t&               count)
{
    // The chip only requests accel while in AUTO mode (optical running), which
    // is exactly when mAccelRaw is connected and mXLData is being filled. Serve
    // the requested window from the buffer, independent of which HR sub-sensor
    // a consumer subscribed to.
    LOG_DEBUG("onAccRequest: mXLData=%u time %u - %u(%u), samples %u - %u\n",
              mXLData.size(), startTimestamp, endTimestamp,
              endTimestamp - startTimestamp, minSamples, maxSamples);

    const uint32_t endTimeInclusive = endTimestamp - (1000 / kXLRate);

    uint32_t firstTimestamp = 0;   // debug only
    uint32_t lastTimestamp  = 0;

    while (count < maxSamples) {
        xl d {};

        if (!mXLData.pop(d, 1)) {
            if (lastTimestamp > 0 && lastTimestamp < endTimeInclusive) {
                LOG_DEBUG("The entire time range is not covered. count %u, "
                          "endTimeInclusive %u, last sample timestamp %u\n",
                          count, endTimeInclusive, lastTimestamp);
            }
            break;  // No more samples
        }

        if (d.ts < startTimestamp) {
            continue;   // Skip samples older than the requested window
        }

        buff[count].x = d.x;
        buff[count].y = d.y;
        buff[count].z = d.z;
        count++;

        if (firstTimestamp == 0) {
            firstTimestamp = d.ts;
        }
        lastTimestamp = d.ts;

        if (count == maxSamples) {
            LOG_DEBUG("Too many samples\n");
            break;  // Remaining samples are read on the next request
        }

        if (d.ts >= endTimeInclusive) {
            break;  // Last sample in the window
        }
    }

    LOG_DEBUG("returned=%u\n", count);

    if (count < minSamples) {
        LOG_WARNING("Request: time %u - %u(%u), samples %u - %u\n",
                    startTimestamp, endTimestamp,
                    endTimestamp - startTimestamp, minSamples, maxSamples);
        LOG_WARNING("Not enough samples: available %u, time %u - %u\n",
                    count, firstTimestamp, lastTimestamp);
    }
}

} /* namespace Sensor */
