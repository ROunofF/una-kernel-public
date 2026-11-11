#define LOG_MODULE_PRX      "TestSensorIMU"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestSensorIMU.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserActivityRecognition.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMotionDetect.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepCounter.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserStepDetector.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometerRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGyroscope.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserGyroscopeRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusion.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserFusionRaw.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserWristMotion.hpp"

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////


class TestSensorIMU : public SDK::Interface::ISensorDataListener
{
public:
    TestSensorIMU()
        : mSensorAccel(SDK::Sensor::Type::ACCELEROMETER, this, 1000 / 50, 1000)
        , mSensorAccelRaw(SDK::Sensor::Type::ACCELEROMETER_RAW, this, 1000.0 / 50.0, 1000)
        , mSensorGyro(SDK::Sensor::Type::GYROSCOPE, this, 1000.0 / 50.0, 1000)
        , mSensorGyroRaw(SDK::Sensor::Type::GYROSCOPE_RAW, this, 1000.0 / 50.0, 1000)
        , mSensorFusion(SDK::Sensor::Type::FUSION, this, 1000.0 / 50.0, 1000)
        , mSensorFusionRaw(SDK::Sensor::Type::FUSION_RAW, this, 1000.0 / 50.0, 1000)
        , mSensorActivityRecognition(SDK::Sensor::Type::ACTIVITY_RECOGNITION, this)
        , mSensorMotionDetect(SDK::Sensor::Type::MOTION_DETECT, this)
        , mSensorStepCounter(SDK::Sensor::Type::STEP_COUNTER, this)
        , mSensorStepDetector(SDK::Sensor::Type::STEP_DETECTOR, this)
        , mSensorWristMotion(SDK::Sensor::Type::WRIST_MOTION, this)
    {
    }

    void run()
    {
//        mSensorAccel.connect();
//        mSensorAccelRaw.connect();
//        mSensorGyro.connect();
//        mSensorGyroRaw.connect();
//        mSensorFusion.connect();
//        mSensorFusionRaw.connect();

        mSensorActivityRecognition.connect();
        mSensorMotionDetect.connect();
        mSensorStepCounter.connect();
        mSensorStepDetector.connect();
        mSensorWristMotion.connect();


        OS::Delay(60000);


//        mSensorAccel.disconnect();
//        mSensorAccelRaw.disconnect();
//        mSensorGyro.disconnect();
//        mSensorGyroRaw.disconnect();
//        mSensorFusion.disconnect();
//        mSensorFusionRaw.disconnect();

        mSensorActivityRecognition.disconnect();
        mSensorMotionDetect.disconnect();
        mSensorStepCounter.disconnect();
        mSensorStepDetector.disconnect();
        mSensorWristMotion.disconnect();
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Sensor::DataBatch batch(base, count, stride);

        if (mSensorAccel.matchesDriver(handle)) {
            SDK::SensorDataParser::Accelerometer p(batch[0]);
            LOG_INFO("accel       : %d-%u (%3.2f : %3.2f : %3.2f)\n", batch.size(),
                                                                      p.getTimestamp(),
                                                                      p.getX(),
                                                                      p.getY(),
                                                                      p.getZ());
        } else if (mSensorAccelRaw.matchesDriver(handle)) {
            SDK::SensorDataParser::AccelerometerRaw p(batch[0]);
            LOG_INFO("accel raw       : %d-%u (%6d : %6d : %6d)\n", batch.size(),
                                                                    p.getTimestamp(),
                                                                    (int) p.getX(),
                                                                    (int) p.getY(),
                                                                    (int) p.getZ());
        } else if (mSensorGyro.matchesDriver(handle)) {
            SDK::SensorDataParser::Gyroscope p(batch[0]);
            LOG_INFO("gyro        : %d-%u (%3.2f : %3.2f : %3.2f)\n", batch.size(),
                                                                      p.getTimestamp(),
                                                                      p.getX(),
                                                                      p.getY(),
                                                                      p.getZ());
        } else if (mSensorGyroRaw.matchesDriver(handle)) {
            SDK::SensorDataParser::GyroscopeRaw p(batch[0]);
            LOG_INFO("gyro raw        : %d-%u (%6d : %6d : %6d)\n", batch.size(),
                                                                    p.getTimestamp(),
                                                                    (int) p.getX(),
                                                                    (int) p.getY(),
                                                                    (int) p.getZ());
        } else if (mSensorFusion.matchesDriver(handle)) {
            SDK::SensorDataParser::Fusion p(batch[0]);
            SDK::SensorDataParser::Fusion::Data data;
            p.getData(data);
            LOG_INFO("fusion      : %d-%u (%3.2f : %3.2f : %3.2f)  (%3.2f : %3.2f : %3.2f)\n", batch.size(),
                                                                                               p.getTimestamp(),
                                                                                               data.accel.x,
                                                                                               data.accel.y,
                                                                                               data.accel.z,
                                                                                               data.gyro.x,
                                                                                               data.gyro.y,
                                                                                               data.gyro.z);
        } else if (mSensorFusionRaw.matchesDriver(handle)) {
            SDK::SensorDataParser::FusionRaw p(batch[0]);
            SDK::SensorDataParser::FusionRaw::Data data;
            p.getData(data);
            LOG_INFO("fusion raw      : %d-%u (%6d : %6d : %6d)  (%6d : %6d : %6d)\n", batch.size(),
                                                                                       p.getTimestamp(),
                                                                                       (int) data.accel.x,
                                                                                       (int) data.accel.y,
                                                                                       (int) data.accel.z,
                                                                                       (int) data.gyro.x,
                                                                                       (int) data.gyro.y,
                                                                                       (int) data.gyro.z);
        } else if (mSensorActivityRecognition.matchesDriver(handle)) {
            SDK::SensorDataParser::ActivityRecognition parser(batch[0]);
            LOG_INFO("activity        : %d\n", static_cast<int>(parser.getID()));
        } else if (mSensorMotionDetect.matchesDriver(handle)) {
            SDK::SensorDataParser::MotionDetect parser(batch[0]);
            LOG_INFO("motion          : %d\n", static_cast<int>(parser.getID()));
        } else if (mSensorStepCounter.matchesDriver(handle)) {
            SDK::SensorDataParser::StepCounter p(batch[0]);
            LOG_INFO("step counts     : %ld\n", p.getStepCount());
        } else if (mSensorStepDetector.matchesDriver(handle)) {
            SDK::SensorDataParser::StepDetector p(batch[0]);
            LOG_INFO("step detect     : +1\n");
        } else if (mSensorWristMotion.matchesDriver(handle)) {
            SDK::SensorDataParser::StepDetector p(batch[0]);
            LOG_INFO("wrist motion    : 1\n");
        }
    }

private:
    Sensor::Connection mSensorAccel;
    Sensor::Connection mSensorAccelRaw;
    Sensor::Connection mSensorGyro;
    Sensor::Connection mSensorGyroRaw;
    Sensor::Connection mSensorFusion;
    Sensor::Connection mSensorFusionRaw;
    Sensor::Connection mSensorActivityRecognition;
    Sensor::Connection mSensorMotionDetect;
    Sensor::Connection mSensorStepCounter;
    Sensor::Connection mSensorStepDetector;
    Sensor::Connection mSensorWristMotion;
};

////////////////////////////////////////////////////////////////////////////////
//// Private data
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Private code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

void TestSensorIMU_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestSensorIMU test;
    OS::Delay(1000);

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        test.run();
        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
