#include "Instances/InstancePAH8316LS.hpp"
#include "Instances/InstanceI2C.hpp"
#include "Gpio.hpp"

namespace Instance {

PAH8316LS& PAH8316LS::getInstance()
{
    static PAH8316LS mInstance;

    return mInstance;
}

PAH8316LS::PAH8316LS()
    : mPAH8316LS(Instance::I2C::getInstance().getBodyBus(),
                 Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_INT1),
                 Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_RST_L),
                 Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_MISO))
    , mFactoryTester(Instance::I2C::getInstance().getBodyBus(),
                     Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_INT1),
                     Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_INT2),
                     Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_RST_L),
                     Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSORBODY_MISO))
{
}

Hardware::PAH8316LS& PAH8316LS::getPAH8316LS()
{
    return mPAH8316LS;
}

Hardware::PAH8316LSFactoryTester& PAH8316LS::getFactoryTester()
{
    return mFactoryTester;
}

}
