#ifndef __INSTANCE_I2C_HPP
#define __INSTANCE_I2C_HPP

#include "I2c.hpp"
#include "I2cRtos.hpp"

namespace Instance {
    class I2C
    {
    public:
        static I2C& getInstance();

        Interface::II2c& getMainBus();
        Interface::II2c& getBodyBus();
        Interface::II2c& getSensor1Bus();
        Interface::II2c& getSensor2Bus();

    private:
        I2C();
        ~I2C() = default;

        Driver::I2c&    mMain;
        Driver::I2cRtos mMainSafe;
        Driver::I2c&    mBody;
        Driver::I2cRtos mBodySafe;
        Driver::I2c&    mSensor1;
        Driver::I2cRtos mSensor1Safe;
        Driver::I2c&    mSensor2;
        Driver::I2cRtos mSensor2Safe;
    };
}

#endif
