#include "Instances/InstanceUART.hpp"

namespace Instance {

UART& UART::getInstance()
{
    static UART mInstance;

    return mInstance;
}

UART::UART()
    : mSensor1(Driver::Uart::GetInstance(Driver::Uart::Port::PORT_1))
    , mSensor1Safe(mSensor1, 1000)
    , mSensor2(Driver::LpUart::GetInstance(Driver::LpUart::Port::PORT_1))
    , mSensor2Safe(mSensor2, 1000)
{
    // Temporary buffer for receiving large amounts of binary data from GPS
    static uint8_t sRxBuffer[10*1024] __attribute__((aligned(32)));
    Driver::LpUart::GetInstance(Driver::LpUart::Port::PORT_1).setBuffer(sRxBuffer, sizeof(sRxBuffer));

//    mSensor1Safe.init();
//    mSensor2Safe.init();
}

Interface::IUart& UART::getSensor1Bus()
{
    return mSensor1Safe;
}

Interface::IUart& UART::getSensor2Bus()
{
    return mSensor2Safe;
}

}
