/**
 ******************************************************************************
 * @file    SensorBody.hpp
 * @brief   Body sensor
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef SENSOR_BODY_HPP
#define SENSOR_BODY_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SensorLayer/Sensors/Body/BodySubSensorHeartRate.hpp"
#include "SensorLayer/Sensors/Body/BodySubSensorHeartRateEx.hpp"
#include "SensorLayer/Sensors/Body/BodySubSensorTouch.hpp"
#include "PAH8316LS.hpp"
#include "IBodySensor.hpp"
#include "SwTimer.hpp"
#include "OS.hpp"

#include <cstdint>
#include <array>
#include <atomic>

using namespace Interface;

namespace Sensor
{
    class Body : public Sensor::ISensorDriverCtrl,
                 public IBodySensor::Callback,
                 public IBodySensor::AccRequestCallback,
                 public SDK::Interface::ISensorDataListener
    {
    public:
        Body(Hardware::PAH8316LS& bodySensor);
        
        Sensor::Driver& getDriverHeartRate();
        Sensor::Driver& getDriverHeartRateEx();
        Sensor::Driver& getDriverTouch();

        /**
         * @brief   Submit an external (strap) heart-rate sample into the source
         *          arbiter. Called by the accessory manager when a
         *          ProfileHrs sample arrives; the arbiter decides whether it or
         *          optical feeds the sensor layer.
         */
        void submitExternalHr(uint8_t hr, uint8_t trustLevel);

        /**
         * @brief   Select external-only HR (no wrist-optical fallback): in this
         *          mode HR is reported invalid whenever the strap has no fresh
         *          reading. Set by the accessory manager from the user's
         *          "External Only" setting; cleared on release.
         */
        void setHrExternalOnly(bool on);

        /**
         * @brief   Forget the last external (strap) HR reading and re-publish.
         *          Called on accessory release so a stale strap sample can't be
         *          preferred for up to the arbiter's freshness window afterwards.
         */
        void clearExternalHr();

        //// ISensorDriverCtrl
        float       sdcStart(Sensor::Driver* driver, float period)        override;
        void        sdcStop(Sensor::Driver* driver)                       override;
        float       sdcUpdatePeriod(Sensor::Driver* driver, float period) override;
        float       sdcGetMinPeriod(Sensor::Driver* driver)               override;
        const char* sdcGetDescription(Sensor::Driver* driver)             override;
        void        sdcNewConnection(Sensor::Driver* driver)              override;

    private:
        enum class OperateMode : uint8_t {
          DISABLE = 0,
          AUTO,
          TOUCH_ONLY
        };

        uint8_t getActiveCount();

        Sensor::ISubSensor* getSubSensor(Sensor::Driver* drive);

        void updateBodySensorMode();

        void onTouch(bool val)                                    override;
        void onHeartRate(uint8_t hr, uint8_t trustLevel)          override;
        void onAccRequest(uint32_t                startTimestamp,
                          uint32_t                endTimestamp,
                          uint32_t                minSamples,
                          uint32_t                maxSamples,
                          IBodySensor::AccSample* buff,
                          uint32_t&               count)          override;

        //// ISensorDataListener - receives the raw accelerometer batches that
        // feed the optical PPG's motion compensation (buffered in mXLData and
        // drained by onAccRequest).
        void onSdlNewData(uint16_t                 handle,
                          const SDK::Sensor::Data* base,
                          uint16_t                 count,
                          uint16_t                 stride)         override;

        // Re-run the arbiter and publish to both HR sub-sensors: the 2-field
        // HEART_RATE (arbitrated) and the 7-field HEART_RATE_EX (arbitrated +
        // source + raw optical + raw external). Applies the off-wrist gate
        // centrally (optical-sourced + raw optical are wrist-gated; external is
        // not).
        void publishHr(uint32_t nowMs);

        // ~1 Hz tick while HR is consumed: re-runs the arbiter so staleness is
        // detected even when no fresh samples arrive (e.g. external-only with
        // optical powered down and the strap gone silent). Drives no I2C.
        void hrTickCallback(void *arg);

        // Heart-rate re-evaluation cadence. Comfortably below the arbiter's
        // 5 s freshness window so a dropout invalidates HR within ~1 s.
        static constexpr uint32_t kHrTickMs = 1000;

        // Optical PPG motion-compensation accelerometer feed. The PAH8316LS HR
        // algorithm requests recent accel windows via onAccRequest; this raw-
        // accel collection lives at the Body level (not inside a HR sub-sensor)
        // so it is available whenever optical HR runs (AUTO mode), regardless of
        // which HR sub-sensor a consumer subscribed to (2-field HEART_RATE or the
        // opt-in HEART_RATE_EX). Connected/disconnected with AUTO mode.
        //
        // A tighter latency makes the source hand its readings over more
        // often, and every hand-over wakes the system. The windows are taken
        // from mXLData after the fact, so a second of readings at a time
        // serves them as well as less would.
        static constexpr uint16_t kXLRate    = 50;   // Hz
        static constexpr uint16_t kXLLatency = 1000; // ms

        struct xl {
            uint32_t ts;
            int16_t  x;
            int16_t  y;
            int16_t  z;
        };

        Hardware::PAH8316LS&             mBodySensor;
        Sensor::BodySubSensorHeartRate   mHeartRate;
        Sensor::BodySubSensorHeartRateEx mHeartRateEx;
        Sensor::BodySubSensorTouch       mTouch;
        Sensor::HrSourceArbiter          mArbiter;
        OS::Mutex                        mHrMutex;
        // Wrist-contact state; read by publishHr (gate) on the arbiter thread,
        // written by onTouch on the chip-callback thread -> atomic.
        std::atomic<bool>                mIsTouched { false };

        // External-only HR: optical PPG is never selected, so it is powered
        // down for the whole mode. Written by setHrExternalOnly, read by
        // updateBodySensorMode (the chip-mode authority). Atomic so the two can
        // run on different threads without sharing mModeMutex with the arbiter.
        std::atomic<bool>              mExternalOnly { false };

        // Serialises the chip-mode transition (mMode + PAH8316LS enable/disable
        // + tick lifecycle), which is driven from both sensor-client threads
        // (sdcStart/sdcStop) and the accessory/app thread (setHrExternalOnly).
        OS::Mutex                      mModeMutex;
        OS::Timer<Body>                mHrTickTimer;

        // Raw-accel connection feeding the PPG (see kXLRate / struct xl above).
        // mXLData is filled on the sensor-data thread (onSdlNewData) and drained
        // on the chip-callback thread (onAccRequest); OS::Queue synchronises them.
        Sensor::Connection             mAccelRaw;
        OS::Queue<xl, kXLRate * 4>     mXLData;

        std::array<Sensor::ISubSensor*, 3> mSubSensors;

        OperateMode mMode;

    }; /* class StubPedo */

} // namespace Sensor

#endif // SENSOR_BODY_HPP
