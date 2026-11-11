#define LOG_MODULE_PRX      "Inst.UsbProtection"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Instances/InstanceUsbProtection.hpp"
#include "Instances/InstanceI2C.hpp"
#include "Gpio.hpp"
#include "UsbProtection.hpp"

namespace Instance {

UsbProtection& UsbProtection::getInstance()
{
    static UsbProtection mInstance;

    return mInstance;
}

bool UsbProtection::init()
{
    ::UsbProtection& usbProtection = ::UsbProtection::GetInstance();
    usbProtection.config(mSTUSB4500,
                         Driver::Gpio::GetInstance(Driver::Gpio::Pin::USB_DAM),
                         Driver::Gpio::GetInstance(Driver::Gpio::Pin::USB_FAULT_L));

    LOG_DEBUG("OK\n");

    return true;
}

UsbProtection::UsbProtection()
    : mSTUSB4500(Instance::I2C::getInstance().getMainBus())
{
}

}
