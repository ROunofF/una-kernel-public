#include "Instances/InstanceI2C.hpp"

namespace Instance {

I2C& I2C::getInstance()
{
    static I2C mInstance;

    return mInstance;
}

I2C::I2C()
    : mMain(Driver::I2c::GetInstance(Driver::I2c::Port::PORT_1))
    , mMainSafe(&mMain, 1000)
    , mBody(Driver::I2c::GetInstance(Driver::I2c::Port::PORT_2))
    , mBodySafe(&mBody, 1000)
    , mSensor1(Driver::I2c::GetInstance(Driver::I2c::Port::PORT_3))
    , mSensor1Safe(&mSensor1, 1000)
    , mSensor2(Driver::I2c::GetInstance(Driver::I2c::Port::PORT_4))
    , mSensor2Safe(&mSensor2, 1000)
{
    mMainSafe.init();
    mBodySafe.init();
    mSensor1Safe.init();
    mSensor2Safe.init();

    // Main bus carries STUSB4500, which is USB-powered and holds SDA low
    // for up to ~190 ms while dying after USB disconnect. Other buses run
    // off system rails and never produce a stuck-bus condition, so the
    // wait stays disabled there.
    mMainSafe.setBusFreeWait(250);
}

Interface::II2c& I2C::getMainBus()
{
    return mMainSafe;
}

Interface::II2c& I2C::getBodyBus()
{
    return mBodySafe;
}

Interface::II2c& I2C::getSensor1Bus()
{
    return mSensor1Safe;
}

Interface::II2c& I2C::getSensor2Bus()
{
    return mSensor2Safe;//mSensor2;
}

}
