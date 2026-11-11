/**
 ******************************************************************************
 * @file    AccelBleLogger.cpp
 * @brief   Redirect data from accelerometer to the BLE NUS Tx
 *
 ******************************************************************************
 */

#include "Loggers/AccelBleLogger.hpp"
#include "System.hpp"

#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGyroscopeRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp"

#define LOG_MODULE_PRX      "AccelBleLogger"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Buzzer.hpp"
#include "BleLogger.hpp"

namespace Component {

/**
 * @brief Construct AccelLogger.
 *
 */
AccelBleLogger::AccelBleLogger()
    : mSrv(nullptr)
    , mDriverFusion(SDK::Sensor::Type::FUSION_RAW, this, 1000.0 / skSampleRateHz, 100)
    , mDriverWrist(SDK::Sensor::Type::WRIST_MOTION, this)
    , mIsStarted(false)
    , mStartTime(0)
    , mWristEvent(0)
    , mWristTs(0)
    , mString()
{
}

void AccelBleLogger::start()
{
    if (mSrv == nullptr || mIsStarted) {
        return;
    }

    BleLogger::GetInstance().start(*mSrv);

    mStartTime = 0;
    mWristEvent = 0;
    mWristTs = 0;

    mDriverFusion.connect();
    mDriverWrist.connect();

    mIsStarted = true;
}

void AccelBleLogger::stop()
{
    if (mSrv == nullptr || !mIsStarted) {
        return;
    }

    mDriverFusion.disconnect();
    mDriverWrist.disconnect();

    BleLogger::GetInstance().stop();

    mIsStarted = false;
}

void AccelBleLogger::onSdlNewData(uint16_t                 handle,
                                const SDK::Sensor::Data* base,
                                uint16_t                 count,
                                uint16_t                 stride)
{
    if (mSrv == nullptr || !mIsStarted) {
        return;
    }

    SDK::Sensor::DataBatch data(base, count, stride);

    // Parse sensor data
    if (mDriverFusion.matchesDriver(handle)) {

        for (uint16_t i = 0; i < data.size(); i++) {
            SDK::SensorDataParser::FusionRaw parser(data[i]);
            if (parser.isDataValid()) {
                if (mStartTime == 0) {
                    mStartTime = parser.getTimestamp();
                }
                uint32_t ts = parser.getTimestamp() - mStartTime;

                SDK::SensorDataParser::FusionRaw::Data data {};
                parser.getData(data);
//                LOG_DEBUG("%d, %d, %d, %d, %d, %d\n",
//                        data.accel.x, data.accel.y, data.accel.z,
//                        data.gyro.x, data.gyro.y, data.gyro.z);

                bool wrist = false;
                if (mWristEvent == 0 && mWristTs!= 0 && mWristTs >= ts) {
                    mWristEvent = 10;
                    mWristTs = 0;
                }
                if (mWristEvent > 0) {
                    wrist = true;
                    mWristEvent--;
                } else  {
                    wrist = false;
                }

                size_t len = std::snprintf(mString, sizeof(mString),
                        "%lu,%d,%d,%d,%d,%d,%d,%d\n",
                        ts,
                        data.accel.x, data.accel.y, data.accel.z,
                        data.gyro.x, data.gyro.y, data.gyro.z,
                        wrist ? 1 : 0);

                BleLogger::GetInstance().write(mString, len);
            }
        }
    } else if (mDriverWrist.matchesDriver(handle)) {
        SDK::SensorDataParser::WristMotion parser(data[0]);
        if (parser.isDataValid()) {
            if (mStartTime == 0) {
                mStartTime = parser.getTimestamp();
            }
            mWristTs = parser.getTimestamp() - mStartTime;
            LOG_DEBUG("Wrist Motion detected at %u\n", parser.getTimestamp());
            Buzzer::GetInstance().play();
        }
    }

}

} // namespace Component
