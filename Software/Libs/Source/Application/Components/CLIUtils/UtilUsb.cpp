/**
 *******************************************************************************
 * @file    UtilUsb.cpp
 * @brief   CLI utility for the USB drive: open or close it with the cable in,
 *          and whether it reopens after a reset.
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "Components/CLIUtils/UtilUsb.hpp"
#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"

#include "Backend.hpp"
#include "SettingsDebugManager.hpp"
#include "UsbDevice.hpp"
#include "main.h"
#if defined(USB_MTP_ENABLE) && (USB_MTP_ENABLE == 1)
#include "MtpResponder.hpp"
#include "usbd_mtp.h"
#endif

#include <string.h>

namespace {

void printStatus()
{
    Backend &backend = Backend::GetInstance();
    const bool open = backend.isMsdOpen();
    const bool reopen =
            Settings::DebugManager::GetInstance().getSettings().usbCfg.reopenAfterReset;

    // With the drive open the device is configured with Mass Storage on,
    // which isHostConfigured() does not report.
    const bool host = open || UsbDevice::GetInstance().isHostConfigured();

    CLIConsole_print("drive:    %s\n", open ? "open" :
                     (backend.isMsdHeld() ? "closed (until 'usb drive on' or unplug)" : "closed"));
    CLIConsole_print("computer: %s\n", host ? "connected" : "not connected");
    CLIConsole_print("reopen:   %s\n", reopen ? "on" : "off");
#if defined(USB_MTP_ENABLE) && (USB_MTP_ENABLE == 1)
    const bool mtp = Settings::DebugManager::GetInstance().getSettings().usbCfg.mtp;
    const MtpResponder &r = MtpResponder::GetInstance();
    CLIConsole_print("mtp:      %s\n", !mtp ? "off" :
                     (open ? "on (paused while the drive is open)" :
                      (r.outOfMemory() ? "on, but not enough free memory (close apps)" :
                       (r.session() != 0 ? "on, in use" : "on"))));
#endif
}

#if defined(USB_MTP_ENABLE) && (USB_MTP_ENABLE == 1)
void printMtpCounters()
{
    const MtpResponder &r = MtpResponder::GetInstance();
    const volatile MtpResponder::Stats &s = r.stats();
    CLIConsole_print("mtp now:  %s, session %lu, ep in 0x%02X out 0x%02X int 0x%02X itf %u\n",
                     r.isOpen() ? "configured" : "not configured",
                     (unsigned long)r.session(), MTP_IN_EP, MTP_OUT_EP, MTP_INT_EP,
                     MTP_ITF_NBR);
    CLIConsole_print("mtp cfg:  on %lu off %lu; setup %lu (last 0x%04lX)\n",
                     (unsigned long)s.cfgOn, (unsigned long)s.cfgOff,
                     (unsigned long)USBD_MTP_SetupCount, (unsigned long)USBD_MTP_LastSetup);
    CLIConsole_print("mtp rx:   armed %lu done %lu (last %lu bytes); commands %lu (last 0x%04lX)\n",
                     (unsigned long)s.rxArmed, (unsigned long)s.rxDone,
                     (unsigned long)s.lastRxLen, (unsigned long)s.commands,
                     (unsigned long)s.lastOp);
    CLIConsole_print("mtp tx:   start %lu done %lu fail %lu; last response 0x%04lX; "
                     "aborts %lu cancels %lu\n",
                     (unsigned long)s.txStart, (unsigned long)s.txDone, (unsigned long)s.txFail,
                     (unsigned long)s.lastResp, (unsigned long)s.aborts,
                     (unsigned long)s.cancels);
    CLIConsole_print("mtp drop: rx dropped %lu; not a command %lu (last 0x%08lX); "
                     "last flags 0x%08lX\n",
                     (unsigned long)s.rxDropped, (unsigned long)s.notCommand,
                     (unsigned long)s.lastHeader, (unsigned long)s.lastFlags);
    CLIConsole_print("mtp poll: %lu polls, %lu events sent, %lu not sent\n",
                     (unsigned long)s.polls, (unsigned long)s.events,
                     (unsigned long)s.eventFails);
    CLIConsole_print("mtp ram:  %lu bytes held; allocated %lu times, failed %lu\n",
                     (unsigned long)r.heapBytes(), (unsigned long)s.allocs,
                     (unsigned long)s.allocFails);
}

void setMtp(bool on)
{
    // As for `usb reopen`: the settings file is the host's while the drive
    // is open.
    Backend &backend = Backend::GetInstance();
    if (backend.isMsdOpen()) {
        CLIConsole_print("the drive is open: run 'usb drive off' first\n");
        return;
    }

    if (Settings::DebugManager::GetInstance().getSettings().usbCfg.mtp == on &&
            UsbDevice::GetInstance().isMtpEnabled() == on) {
        CLIConsole_print(on ? "mtp already on\n" : "mtp already off\n");
        return;
    }

    using Result = Backend::ReopenResult;
    if (backend.mtpResult() == Result::Pending) {
        CLIConsole_print("the last 'usb mtp' is still pending; try again\n");
        return;
    }
    if (!backend.setMtp(on)) {
        CLIConsole_print("failed to post event\n");
        return;
    }

    Result result = backend.mtpResult();
    for (int waited = 0; result == Result::Pending && waited < 3000; waited += 10) {
        WAIT(10);
        result = backend.mtpResult();
    }

    switch (result) {
    case Result::Saved:
        // The Backend re-enumerates the device just after saying so.
        CLIConsole_print(on ? "mtp: on; this port reconnects\n"
                            : "mtp: off; this port reconnects\n");
        break;
    case Result::DriveOpen:
        CLIConsole_print("not saved: the drive opened; run 'usb drive off' first\n");
        break;
    case Result::SaveFailed:
        CLIConsole_print("not saved: writing the settings failed\n");
        break;
    case Result::Pending:
        CLIConsole_print("still pending; run 'usb' to check\n");
        break;
    }
}
#endif

void driveOn()
{
    Backend &backend = Backend::GetInstance();
    if (backend.isMsdOpen()) {
        CLIConsole_print("drive already open\n");
        return;
    }
    if (!UsbDevice::GetInstance().isHostConfigured()) {
        CLIConsole_print("no computer connected\n");
        return;
    }

    CLIConsole_print(backend.openMsdByHand()
            ? "opening the drive; this port reconnects\n"
            : "failed to post event\n");
}

void driveOff()
{
    Backend &backend = Backend::GetInstance();
    if (!backend.isMsdOpen()) {
        CLIConsole_print("drive not open\n");
        return;
    }

    // Printed first: closing the drive re-enumerates the device, and this
    // port goes with it.
    CLIConsole_print("closing the drive; this port reconnects\n");
    WAIT(100);    // Let the line out before the port drops, as `reset` does.
    if (!backend.closeMsdByHand()) {
        CLIConsole_print("failed to post event\n");
    }
}

void setReopen(bool on)
{
    // The settings file is on the volume the host owns while the drive is
    // open. Checked here for the message; the Backend checks again as it
    // saves, on the thread that opens the drive.
    Backend &backend = Backend::GetInstance();
    if (backend.isMsdOpen()) {
        CLIConsole_print("the drive is open: run 'usb drive off' first\n");
        return;
    }

    using Result = Backend::ReopenResult;
    if (backend.reopenResult() == Result::Pending) {
        CLIConsole_print("the last 'usb reopen' is still pending; try again\n");
        return;
    }
    if (!backend.setReopenAfterReset(on)) {
        CLIConsole_print("failed to post event\n");
        return;
    }

    // Report what the Backend did, not that the request was queued: it
    // refuses if the drive opened meanwhile, and the save can fail.
    Result result = backend.reopenResult();
    for (int waited = 0; result == Result::Pending && waited < 3000; waited += 10) {
        WAIT(10);
        result = backend.reopenResult();
    }

    switch (result) {
    case Result::Saved:
        CLIConsole_print(on ? "reopen: on\n" : "reopen: off\n");
        break;
    case Result::DriveOpen:
        CLIConsole_print("not saved: the drive opened; run 'usb drive off' first\n");
        break;
    case Result::SaveFailed:
        CLIConsole_print("not saved: writing the settings failed\n");
        break;
    case Result::Pending:
        CLIConsole_print("still pending; run 'usb' to check\n");
        break;
    }
}

bool parseOnOff(const char *arg, bool &on)
{
    if (strcmp(arg, "on") == 0) {
        on = true;
        return true;
    }
    if (strcmp(arg, "off") == 0) {
        on = false;
        return true;
    }
    return false;
}

} // namespace

/**
 * @brief Construct a new Components::CliUtils::UtilUsb object
 */
Components::CliUtils::UtilUsb::UtilUsb() :
        UtilBase("usb"),
        mCommands{
            .execute = UtilUsb::execute,
            .getName = UtilUsb::getName,
            .getDesc = UtilUsb::getDesc,
            .getHelp = UtilUsb::getHelp,
        }
{

}

/**
 * @brief  Returns the reference to a single instance of the utility
 *
 * @return A reference to the utility object
 */
Components::CliUtils::UtilUsb& Components::CliUtils::UtilUsb::getInstance()
{
    static UtilUsb instance;
    return instance;
}

/**
 * @brief  Returns a pointer to the structure with pointers to static command
 *         handlers
 *
 * @return A pointer to the ICLICommand_t
 */
ICLICommand_t *Components::CliUtils::UtilUsb::getCommands()
{
    return &mCommands;
}

/**
 * @brief  Returns the utility name
 *
 * @param  pCmd: A pointer to the command handler functions
 *
 * @return Utility name
 */
const char* Components::CliUtils::UtilUsb::getName(ICLICommand_t* cmd)
{
    return UtilUsb::getInstance().getUtilName();
}

/**
 * @brief  Returns a brief utility description
 *
 * @param  pCmd: A pointer to the command handler functions
 *
 * @return Utility description
 */
const char* Components::CliUtils::UtilUsb::getDesc(ICLICommand_t* cmd)
{
    return "opens/closes the USB drive";
}

/**
 * @brief  Provides a help on how to use the utility
 *
 * @param  pCmd: A pointer to the command handler functions
 *
 * @return Empty ""
 */
const char* Components::CliUtils::UtilUsb::getHelp(ICLICommand_t* cmd)
{
    CLIConsole_print(
        "Open or close the USB drive with the cable in\n"
        "\n"
        "How to run:\n"
            "status          - " CLI_ATTR_CMD_HELP "usb" CLI_ATTR_DEF "\n"
            "open the drive  - " CLI_ATTR_CMD_HELP "usb drive on" CLI_ATTR_DEF "\n"
            "close the drive - " CLI_ATTR_CMD_HELP "usb drive off" CLI_ATTR_DEF "\n"
            "open it again after 'reset' with the cable in (default off, saved)\n"
            "                - " CLI_ATTR_CMD_HELP "usb reopen on|off" CLI_ATTR_DEF "\n"
#if defined(USB_MTP_ENABLE) && (USB_MTP_ENABLE == 1)
            "share the files over MTP while the drive is closed (default off,\n"
            "saved; plugging in then leaves the drive closed)\n"
            "                - " CLI_ATTR_CMD_HELP "usb mtp on|off" CLI_ATTR_DEF "\n"
            "MTP counters    - " CLI_ATTR_CMD_HELP "usb mtp" CLI_ATTR_DEF "\n"
#endif
        "\n"
        "Plugging the cable in, or turning the watch on with it in, opens\n"
        "the drive.\n"
        "'usb drive off' does what unplugging does: the settings are reloaded\n"
        "and the apps come back. Flush the drive first (Windows:\n"
        "Write-VolumeCache <letter>; Linux/macOS: sync). The drive stays\n"
        "closed until 'usb drive on' or the cable is unplugged.\n"
        "Both re-enumerate the device, so this serial port reconnects.\n"
        "\n"
        "App loop with the cable in: 'usb drive on', copy the app, flush the\n"
        "drive, 'reset'.\n"
        "With reopen off the watch comes back usable, with the new app.\n"
#if defined(USB_MTP_ENABLE) && (USB_MTP_ENABLE == 1)
        "\n"
        "With MTP on the watch shows on the computer as a device (Explorer:\n"
        "This PC), like a phone: files can be copied both ways while the\n"
        "watch keeps running and writing. The drive is still there for tools\n"
        "that need a drive letter: 'usb drive on', then 'usb drive off'.\n"
#endif
        );

    return "";
}

/**
 * @brief  Executes the request to the utility
 *
 * @param  cmd:   Pointer to the command handler functions
 * @param  input: Pointer to the CLI input
 * @param  argc:  Count of the arguments
 * @param  argv:  Pointer to the arguments
 */
void Components::CliUtils::UtilUsb::execute(ICLICommand_t* cmd,
                                            CLIInput_t*    input,
                                            int            argc,
                                            const char**   argv)
{
    (void) cmd;
    (void) input;

    bool on = false;

    if (argc == 1) {
        printStatus();
    } else if (argc == 3 && strcmp(argv[1], "drive") == 0 && parseOnOff(argv[2], on)) {
        if (on) {
            driveOn();
        } else {
            driveOff();
        }
    } else if (argc == 3 && strcmp(argv[1], "reopen") == 0 && parseOnOff(argv[2], on)) {
        setReopen(on);
#if defined(USB_MTP_ENABLE) && (USB_MTP_ENABLE == 1)
    } else if (argc == 2 && strcmp(argv[1], "mtp") == 0) {
        printMtpCounters();
    } else if (argc == 3 && strcmp(argv[1], "mtp") == 0 && parseOnOff(argv[2], on)) {
        setMtp(on);
    } else {
        CLIConsole_print("usage: usb | usb drive on|off | usb reopen on|off | usb mtp [on|off]\n");
#else
    } else {
        CLIConsole_print("usage: usb | usb drive on|off | usb reopen on|off\n");
#endif
    }
}
