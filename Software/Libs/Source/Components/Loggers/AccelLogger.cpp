/**
 ******************************************************************************
 * @file    AccelLogger.cpp
 * @brief   Redirect data from accelerometer to the CSV file
 *
 ******************************************************************************/
#include "Loggers/AccelLogger.hpp"
#include "Directory.hpp"
#include "System.hpp"

#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGyroscopeRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp"

#define LOG_MODULE_PRX      "AccelLogger"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Buzzer.hpp"

namespace Component {

/**
 * @brief Construct AccelLogger.
 *
 */
AccelLogger::AccelLogger()
    : mLogger(mHeader)
    , mDriverFusion(SDK::Sensor::Type::FUSION_RAW, this, 1000.0 / 50.0, 1000)
    , mDriverAccel(SDK::Sensor::Type::ACCELEROMETER_RAW, this, 1000.0 / 50.0, 1000)
    , mDriverGyro(SDK::Sensor::Type::GYROSCOPE_RAW, this, 1000.0 / 50.0, 1000)
    , mDriverWrist(SDK::Sensor::Type::WRIST_MOTION, this)
    , mStartTime(0)
    , mWristEvent(0)
    , mWristTs(0)
{

}

void AccelLogger::start()
{
    if (mLogger.isOpened()) {
        return;
    }

    // accel_<id>_<NNNN>_<ts>.csv with per-start retention. NNNN is allocated
    // and old sessions pruned inside open().
    mLogger.setNaming(mFolderName, "accel", Driver::System::GetDeviceIdStr());
    mLogger.setRetention(skKeepNewest);

    mStartTime = 0;
    mWristEvent = 0;
    mWristTs = 0;

    if (mLogger.open()) {
        mDriverFusion.connect();
        //mDriverAccel.connect();
        //mDriverGyro.connect();
        mDriverWrist.connect();
    }
}

void AccelLogger::stop()
{
    if (!mLogger.isOpened()) {
        return;
    }

    mDriverFusion.disconnect();
    //mDriverAccel.disconnect();
    //mDriverGyro.disconnect();
    mDriverWrist.disconnect();

    mLogger.close();
}

void AccelLogger::onSdlNewData(uint16_t                 handle,
                                const SDK::Sensor::Data* base,
                                uint16_t                 count,
                                uint16_t                 stride)
{
    if (!mLogger.isOpened()) {
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

                mLogger.startRecord();
                mLogger.append(ts);
                // Accel
                mLogger.append(data.accel.x);
                mLogger.append(data.accel.y);
                mLogger.append(data.accel.z);
                // Gyro
                mLogger.append(data.gyro.x);
                mLogger.append(data.gyro.y);
                mLogger.append(data.gyro.z);

                if (mWristEvent == 0 && mWristTs!= 0 && mWristTs >= ts) {
                    mWristEvent = 10;
                    mWristTs = 0;
                }
                if (mWristEvent > 0) {
                    mLogger.append((int16_t)10000);
                    mWristEvent--;
                } else  {
                    mLogger.append(false);
                }

                mLogger.stopRecord();
            }
        }
    } else if (mDriverAccel.matchesDriver(handle)) {

    } else if (mDriverGyro.matchesDriver(handle)) {

    } else if (mDriverWrist.matchesDriver(handle)) {
        SDK::SensorDataParser::WristMotion parser(data[0]);
        if (parser.isDataValid()) {
            if (mStartTime == 0) {
                mStartTime = parser.getTimestamp();
            }
            mWristTs = parser.getTimestamp() - mStartTime;
            LOG_DEBUG("Wrist Motion detected\n");
            Buzzer::GetInstance().play();
        }
    }

}

} // namespace Component
