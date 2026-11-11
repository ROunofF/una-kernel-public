#define LOG_MODULE_PRX      "SensorLayerMsg"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Test/TestSensorLayerMessage.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"

#include "Instances/InstanceSensorLayer.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserTemperature.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserPressure.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAltimeter.hpp"

#include <string.h>
#include <stdlib.h>

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

class TestSensorAltimeterMsg : public SDK::Interface::ISensorDataListener
{
public:
    TestSensorAltimeterMsg()
        : mSensorT(0)
        , mSensorP(0)
        , mSensorAltimeter(0)
    {
    }

    void run()
    {
        mSensorT         = connect(SDK::Sensor::Type::AMBIENT_TEMPERATURE, this);
        mSensorP         = connect(SDK::Sensor::Type::PRESSURE, this);
        mSensorAltimeter = connect(SDK::Sensor::Type::ALTIMETER, this);

        ::Driver::SwTimer t(10000);
        for (;;) {
            OS::Delay(100);
            if (t.expired()) {
                break;
            }
        }

        disconnect(mSensorT, this);
        disconnect(mSensorP, this);
        disconnect(mSensorAltimeter, this);
    }

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override
    {
        SDK::Message::Sensor::EventData event;
        event.handle = handle;
        event.count  = count;
        event.stride = stride;
        memcpy(event.data, (void*) base, count * stride);

        handlerEvent(event);
    }

    static void handlerRequest(SDK::Message::Sensor::RequestList& req)
    {
        auto list = Sensor::Manager::getInstance().getHandleList(req.id);

        if (list.size() > ARRAY_SIZE(req.handles)) {
            req.setResult(SDK::MessageResult::FAIL);
            return;
        }

        req.handlesCount = list.size();
        for (uint8_t idx = 0; idx < list.size(); ++idx) {
            req.handles[idx] = list[idx];
        }

        req.setResult(SDK::MessageResult::SUCCESS);
    }

    static void handlerRequest(SDK::Message::Sensor::RequestDefault& req)
    {
        Sensor::Driver* driver = Sensor::Manager::getInstance().getDefaultSensor(req.id);

        if (!driver) {
            req.setResult(SDK::MessageResult::FAIL);
            return;
        }

        req.handle = driver->getHandle();

        req.setResult(SDK::MessageResult::SUCCESS);
    }

    static void handlerRequest(SDK::Message::Sensor::RequestGetDesc& req)
    {
        Sensor::Driver* driver = Sensor::Manager::getInstance().getDriverByHandle(req.handle);

        if (!driver) {
            req.setResult(SDK::MessageResult::FAIL);
            return;
        }

        strncpy(req.desc, driver->getDescription(), sizeof(req.desc));

        req.setResult(SDK::MessageResult::SUCCESS);
    }


    void handlerRequest(SDK::Message::Sensor::RequestConnect& req)
    {
        Sensor::Driver* driver = Sensor::Manager::getInstance().getDriverByHandle(req.handle);

        if (!driver) {
            req.setResult(SDK::MessageResult::FAIL);
            return;
        }

        if (!driver->connect(this, req.period, req.latency)) {
            req.setResult(SDK::MessageResult::FAIL);
            return;
        }

        req.setResult(SDK::MessageResult::SUCCESS);
    }

    void handlerRequest(SDK::Message::Sensor::RequestDisconnect& req)
    {
        Sensor::Driver* driver = Sensor::Manager::getInstance().getDriverByHandle(req.handle);

        if (!driver) {
            req.setResult(SDK::MessageResult::FAIL);
            return;
        }

        driver->disconnect(this);

        req.setResult(SDK::MessageResult::SUCCESS);
    }

private:
    uint32_t connect(SDK::Sensor::Type id, SDK::Interface::ISensorDataListener* listener)
    {
        SDK::Message::Sensor::RequestDefault reqDefault;
        reqDefault.id = id;

        handlerRequest(reqDefault);

        if (reqDefault.getResult() == SDK::MessageResult::FAIL) {
            return 0;
        }

        SDK::Message::Sensor::RequestConnect reqConnect;
        reqConnect.handle   = reqDefault.handle;

        handlerRequest(reqConnect);

        if (reqConnect.getResult() == SDK::MessageResult::FAIL) {
            return 0;
        }

        return reqConnect.handle;
    }

    void disconnect(uint32_t handle, SDK::Interface::ISensorDataListener* listener)
    {
        SDK::Message::Sensor::RequestDisconnect reqDisconnect;
        reqDisconnect.handle   = handle;

        handlerRequest(reqDisconnect);
    }

    void handlerEvent(SDK::Message::Sensor::EventData& event)
    {
        SDK::Sensor::DataBatch batch((const SDK::Sensor::Data*)event.data, event.count, event.stride);

        if (mSensorT == event.handle) {
            SDK::SensorDataParser::Temperature p(batch[0]);
            LOG_INFO("temperature     : %.2f\n", p.getTemperature());
        } else if (mSensorP == event.handle) {
            SDK::SensorDataParser::Pressure p(batch[0]);
            LOG_INFO("pressure        : %.2f\n", p.getPressure());
        } else if (mSensorAltimeter == event.handle) {
            SDK::SensorDataParser::Altimeter p(batch[0]);
            LOG_INFO("altitude        : %.2f\n", p.getAltitude());
        }
    }

    uint32_t mSensorT;
    uint32_t mSensorP;
    uint32_t mSensorAltimeter;
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

void TestSensorLayerMessage_execute()
{
    LOG_INFO("start\n");

    Instance::SensorLayer::getInstance().init();

    TestSensorAltimeterMsg test;

    while (1) {
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
        LOG_INFO_WP("//// TESTING\n");
        LOG_INFO_WP("////////////////////////////////////////////////////////////\n");

        ///////////////////////////////////////////////////////////
        //// Get sensor list by ID
        ///////////////////////////////////////////////////////////

        SDK::Message::Sensor::RequestList msgRequestList;
        msgRequestList.id = SDK::Sensor::Type::ALTIMETER;

        TestSensorAltimeterMsg::handlerRequest(msgRequestList);

        if (msgRequestList.getResult() == SDK::MessageResult::FAIL) {
            return;
        }

        for (uint32_t idx = 0; idx < msgRequestList.handlesCount; ++idx) {
            LOG_INFO("handle %d = %d\n", idx, msgRequestList.handles[idx]);
        }

        ///////////////////////////////////////////////////////////
        //// Get default sensor by ID
        ///////////////////////////////////////////////////////////

        SDK::Message::Sensor::RequestDefault msgRequestDefault;
        msgRequestDefault.id = SDK::Sensor::Type::AMBIENT_TEMPERATURE;

        TestSensorAltimeterMsg::handlerRequest(msgRequestDefault);

        if (msgRequestDefault.getResult() == SDK::MessageResult::FAIL) {
            return;
        }

        LOG_INFO("default handle of AMBIENT_TEMPERATURE = %d\n", (int)msgRequestDefault.handle);

        ///////////////////////////////////////////////////////////
        //// Get sensor description
        ///////////////////////////////////////////////////////////

        SDK::Message::Sensor::RequestGetDesc msgRequestDesc;
        msgRequestDesc.handle = msgRequestDefault.handle;

        TestSensorAltimeterMsg::handlerRequest(msgRequestDesc);

        if (msgRequestDesc.getResult() == SDK::MessageResult::FAIL) {
            return;
        }

        LOG_INFO("driver desc [%s]\n", msgRequestDesc.desc);

        ///////////////////////////////////////////////////////////
        //// Get sensors data test
        ///////////////////////////////////////////////////////////

        test.run();

        LOG_INFO_WP("\n\n");

        OS::Delay(3000);
    }
}
