/**
 *******************************************************************************
 * @file    UtilUsbMsd.cpp
 * @brief   CLI utility to manage drives. Allows mounting, unmounting and
 *          accessing the device Mass Storage disk over USB.
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#define LOG_MODULE_PRX   "UtilUsbMsd"
#define LOG_MODULE_LEVEL LOG_LEVEL_INFO
#include "Logger.h"

#include "Components/CLIUtils/UtilUsbMsd.hpp"
#include "CLI/CLIConsole.h"
#include "CLI/CLIParser.h"
#include "SwTimer.hpp"
#include "Pmic.hpp"
#include "flash_diskio.h"

#include <string.h>

namespace Components::CliUtils {

/**
 * @brief Current mounted disk index.
 *
 * Holds the ID of the currently mounted disk (0,1,2) or 0xFF if unmounted.
 */

uint8_t UtilUsbMsd::mDisk = 0xFF;

/**
 * @brief Constructor
 */
UtilUsbMsd::UtilUsbMsd() :
    UtilBase("msd"),
    mCommands{
        .execute = UtilUsbMsd::execute,
        .getName = UtilUsbMsd::getName,
        .getDesc = UtilUsbMsd::getDesc,
        .getHelp = UtilUsbMsd::getHelp,
    }
{}

/**
 * @brief Get singleton instance of the utility.
 * @return Reference to the utility object.
 */
Components::CliUtils::UtilUsbMsd& UtilUsbMsd::getInstance()
{
    static UtilUsbMsd instance;

    return instance;
}

/**
 * @brief Get the table with pointers to the command handlers.
 * @return Pointer to ICLICommand_t.
 */
ICLICommand_t* UtilUsbMsd::getCommands()
{
    return &mCommands;
}

/**
 * @brief Get the utility name
 * @param cmd Unused.
 * @return Utility name string.
 */
const char* UtilUsbMsd::getName(ICLICommand_t* cmd)
{
    (void) cmd;

    return UtilUsbMsd::getInstance().getUtilName();
}

/**
 * @brief Get a short description of the utility.
 * @param cmd Unused.
 * @return Brief description string.
 */
const char* UtilUsbMsd::getDesc(ICLICommand_t* cmd)
{
    (void) cmd;

    return "mount and access device mass storage disks";
}

/**
 * @brief Print help/usage for the utility.
 * @param cmd Unused.
 * @return NUL-terminated help text (owned by the function).
 */
const char* UtilUsbMsd::getHelp(ICLICommand_t* cmd)
{
    (void) cmd;

    static const char* help =
        "USB Mass Storage Utility\n"
        "\n"
        "How to run:\n"
        "\n"
            "mount the specified drive - " CLI_ATTR_CMD_HELP "msd -mount drive" CLI_ATTR_DEF "\n"
            "unmount current drive     - " CLI_ATTR_CMD_HELP "msd -unmount" CLI_ATTR_DEF "\n"
            "show current status       - " CLI_ATTR_CMD_HELP "msd" CLI_ATTR_DEF "\n"
        "\n"
        " Available drives:\n"
            "   - " CLI_ATTR_CMD_HELP "0" CLI_ATTR_DEF " : UNA BOOT drive\n"
            "   - " CLI_ATTR_CMD_HELP "1" CLI_ATTR_DEF " : UNA SYS drive\n"
            "   - " CLI_ATTR_CMD_HELP "2" CLI_ATTR_DEF " : UNA WATCH drive\n"
        "\n"
        "Examples\n"
        "\n"
        "     " CLI_ATTR_CMD_HELP "msd -mount 0" CLI_ATTR_DEF " : mount UNA BOOT drive\n"
        "     " CLI_ATTR_CMD_HELP "msd -unmount" CLI_ATTR_DEF " : unmount current drive\n"
        "     " CLI_ATTR_CMD_HELP "msd         " CLI_ATTR_DEF " : show status\n";

    return help;
}

/**
 * @brief Wait until USB host connects and sends any command.
 *
 * @param usb   Reference to UsbDevice instance.
 * @return true if connection established, false if timeout or USB disconnected.
 */
bool UtilUsbMsd::waitConnection(UsbDevice& usb)
{
    Pmic& pmic = Pmic::GetInstance();

    LOG_WARNING("Waiting for host connection...\n");
    Driver::SwTimer t(10000);
    while (true) {
        OS::Delay(500);
        if (usb.isSerialPortOpen()) {
            return true;
        }

        if (t.check() || !pmic.isUsbDetected()) {
            return false;
        }
    }
}

/**
 * @brief Mount the specified disk and expose it as USB Mass Storage.
 *
 * @param disk Disk index (0=bootloader, 1=system, 2=user).
 */
void UtilUsbMsd::cmdMount(uint8_t disk)
{
    static const uint8_t map[] = {FLASH_DISK_UPD,
                                  FLASH_DISK_SYS,
                                  FLASH_DISK_USR};

    if (mDisk == disk) {
        CLIConsole_print("the disk is already mounted\n");
        return;
    }

    UsbDevice& usb = UsbDevice::GetInstance();

    usb.deinit();

    mDisk = disk;

    usb.init(true, map[mDisk]);
    usb.enable();

    if (!waitConnection(usb)) {
        LOG_INFO("system reset\n");
        OS::Delay(100);
        NVIC_SystemReset();
    }
}

/**
 * @brief Unmount the currently mounted disk.
 */
void UtilUsbMsd::cmdUnmount()
{
    if (mDisk == 0xFF) {
        return;
    }

    UsbDevice& usb = UsbDevice::GetInstance();

    LOG_INFO("%d\n", __LINE__);
    usb.disable();
    LOG_INFO("%d\n", __LINE__);
    usb.deinit();
    LOG_INFO("%d\n", __LINE__);

    OS::Delay(500);
    LOG_INFO("%d\n", __LINE__);

    mDisk = 0xFF;

    usb.init(false, 0);
    LOG_INFO("%d\n", __LINE__);
    usb.enable();
    LOG_INFO("%d\n", __LINE__);

    if (!waitConnection(usb)) {
        LOG_INFO("system reset\n");
        OS::Delay(100);
        NVIC_SystemReset();
    }
}

/**
 * @brief Show current mount status.
 *
 * Prints either "unmounted" or the current disk index.
 */
void UtilUsbMsd::cmdShowStatus()
{
    if (mDisk == 0xFF) {
        CLIConsole_print("unmounted\n");
    } else {
        CLIConsole_print("disk=%d\n", (int)mDisk);
    }
}

/**
 * @brief Execute the USB Mass Storage utility: parse arguments, dispatch to handlers.
 *
 * @param cmd   Unused.
 * @param input Unused (console input context).
 * @param argc  Argument count (command name + optional switches).
 * @param argv  Argument values.
 *
 * Supported options:
 *   - "-mount <disk>" : mount specified disk (0,1,2)
 *   - "-unmount"      : unmount current disk
 *   - no arguments    : show status
 */
void UtilUsbMsd::execute(ICLICommand_t* cmd,
                        CLIInput_t*    input,
                        int            argc,
                        const char**   argv)
{
    (void) cmd;
    (void) input;

    CLIParser_t p;
    CLIParser_init(&p, argc, argv);

    if (CLIParser_check(&p, "-mount") && argc == 3) {
        uint32_t disk;
        if (!CLIParser_getValueAsUI32(&p, "-mount", &disk) || disk > 2) {
            CLIConsole_print("invalid parameter value\n");
            return;
        }

        cmdMount(disk);
    } else if (CLIParser_check(&p, "-unmount") && argc == 2) {
        cmdUnmount();
    } else if (argc == 1) {
        cmdShowStatus();
    }
}

} // namespace Components::CliUtils
