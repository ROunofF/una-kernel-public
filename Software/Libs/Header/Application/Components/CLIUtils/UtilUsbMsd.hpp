/**
 *******************************************************************************
 * @file    UtilUsbMsd.cpp
 * @brief   CLI utility to manage drives. Allow to mount and access
 *          the device Mass Storage disk over USB.
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#pragma once

#include "Porting.h"
#include "CLI/UtilBase.hpp"
#include "CLI/ICommand.h"
#include "UsbDevice.hpp"

#include <memory>

namespace Components {

namespace CliUtils {

class UtilUsbMsd : public UtilBase
{
public:
    static UtilUsbMsd &getInstance();

    UtilUsbMsd(UtilUsbMsd const&)       = delete;
    void operator=(UtilUsbMsd const&) = delete;

    UtilUsbMsd(UtilUsbMsd&&)            = delete;
    UtilUsbMsd& operator=(UtilUsbMsd&&) = delete;

    ICLICommand_t *getCommands() override;

private:
    ICLICommand_t mCommands;

    explicit UtilUsbMsd();
    ~UtilUsbMsd() override = default;

    static bool waitConnection(UsbDevice& usb);

    static void cmdMount(uint8_t disk);
    static void cmdUnmount();
    static void cmdShowStatus();

    static const char* getName(ICLICommand_t* cmd);
    static const char* getDesc(ICLICommand_t* cmd);
    static const char* getHelp(ICLICommand_t* cmd);
    static void        execute(ICLICommand_t* cmd,
                               CLIInput_t*    input,
                               int            argc,
                               const char**   argv);

    static uint8_t mDisk;
};

}
}
