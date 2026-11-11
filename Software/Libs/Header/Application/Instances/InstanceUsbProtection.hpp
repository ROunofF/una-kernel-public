#ifndef __INSTANCE_USB_PROTECTION_HPP
#define __INSTANCE_USB_PROTECTION_HPP

#include "STUSB4500.hpp"

namespace Instance {
    class UsbProtection
    {
    public:
        static UsbProtection& getInstance();

        bool init();

    private:
        UsbProtection();
        ~UsbProtection() = default;

        Hardware::STUSB4500 mSTUSB4500;
    };
}

#endif
