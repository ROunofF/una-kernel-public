/**
 ******************************************************************************
 * @file    ProdTestUSB.сpp
 * @brief   Production test for the USB subsystem.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.USB"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"


#include "ProdTest/ProdTestUSB.hpp"
#include "Instances/InstanceI2C.hpp"
#include "CLI/CLIConsole.h"
#include "CLI/CLIParser.h"
#include "Gpio.hpp"
#include "OS.hpp"
#include "SwTimer.hpp"
#include "FuelGauge.hpp"
#include "Power.hpp"

namespace ProdTest
{

////////////////////////////////////////////////////////////////////////////////
//// UsbProdTestFSM class
////////////////////////////////////////////////////////////////////////////////

UsbProdTestFSM::UsbProdTestFSM(IUsbProdTestFSMPort& port)
    : Interface::IFSM<IUsbProdTestFSMPort::State, IUsbProdTestFSMPort::Event, IUsbProdTestFSMPort::Flag>(0, 0)
    , mPort(port)
{
}

void UsbProdTestFSM::processing(IUsbProdTestFSMPort::Event e)
{
    switch (getState()) {
    case IUsbProdTestFSMPort::State::DEFAULT:
        if (mPort.x0()) {
            setState(IUsbProdTestFSMPort::State::ERROR);
        } else if (mPort.x1()) {
            setState(IUsbProdTestFSMPort::State::CONNECTED);
        } else if (mPort.x2()) {
            setState(IUsbProdTestFSMPort::State::DAM);
        }
        break;

    case IUsbProdTestFSMPort::State::CONNECTED:
        if (!mPort.x1()) {
            setState(IUsbProdTestFSMPort::State::DEFAULT);
        }
        break;

    case IUsbProdTestFSMPort::State::DAM:
        if (!mPort.x2()) {
            setState(IUsbProdTestFSMPort::State::DEFAULT);
        }
        break;

    case IUsbProdTestFSMPort::State::ERROR:
        if (!mPort.x0()) {
            setState(IUsbProdTestFSMPort::State::DEFAULT);
        }
        break;

    default: assert(false); break;
    }
}

void UsbProdTestFSM::processingNewState()
{
    switch (getState()) {
    case IUsbProdTestFSMPort::State::DEFAULT:
            LOG_DEBUG("[DEFAULT]\n");
            mPort.z0();
        break;

    case IUsbProdTestFSMPort::State::CONNECTED:
            LOG_DEBUG("[CONNECTED]\n");
            mPort.z1();
        break;

    case IUsbProdTestFSMPort::State::DAM:
            LOG_DEBUG("[DAM]\n");
            mPort.z2();
        break;

    case IUsbProdTestFSMPort::State::ERROR:
            LOG_DEBUG("[ERROR]\n");
            mPort.z3();
        break;

    default: assert(false); break;
    }
}

////////////////////////////////////////////////////////////////////////////////
//// USB class
////////////////////////////////////////////////////////////////////////////////

USB::USB(const std::string& id)
    : Interface::IProdTest(id)
    , mFSM(*this)
    , mLCD()
{}

bool USB::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test USB Protection...\n");

    if (!checkIC(slog)) {
        LOG_ERROR("failed to check the ICs\n");
        return false;
    }

    LOG_DEBUG("the IC are ok\n");

    if (!mLCD.init(slog)) {
        LOG_ERROR("failed to init the LCD\n");
        return false;
    }

    LOG_DEBUG("the LCD is ok\n");

    //// Config the FuelGauge
    ::FuelGauge& fuelGauge = ::FuelGauge::GetInstance();
    fuelGauge.config(Instance::I2C::getInstance().getMainBus());

    // Run Power manager
    Power& power = Power::GetInstance();
    power.run();

    mFSM.reset();

    mLCD.setColor(Interface::ILcd::Color::BLACK);

    LOG_DEBUG("start the test loop\n");

    CLIConsole_print(CLI_UTIL_PROMPT);

    while (true) {
        OS::Delay(100);

        if (CLIInput_refresh(input) == true) {
            if (strcmp(CLIInput_getArgV(input)[0], "exit") == 0) {
                break;
            }

            checkCommand(input);

            CLIInput_reset(input);
            CLIConsole_print(CLI_UTIL_PROMPT);
        }

//        mFSM.refresh();
    }

    mLCD.setColor(Interface::ILcd::Color::BLACK);

    mLCD.deinit();

    power.stop();

    return true;
}

bool USB::x0() //// Fault
{
    return UsbProtection::GetInstance().isUsbFault();
}

bool USB::x1() //// Sink
{
    return UsbProtection::GetInstance().isSink();
}

bool USB::x2() //// DAM
{
    return UsbProtection::GetInstance().isDAM();
}

void USB::z0() //// Fill the LCD in black
{
    mLCD.setColor(Interface::ILcd::Color::BLACK);
}

void USB::z1() //// Fill the LCD in green
{
    mLCD.setColor(Interface::ILcd::Color::GREEN);
}

void USB::z2() //// Fill the LCD in blue
{
    mLCD.setColor(Interface::ILcd::Color::BLUE);
}

void USB::z3() //// Fill the LCD in red
{
    mLCD.setColor(Interface::ILcd::Color::RED);
}

bool USB::checkIC(StringLogger& slog)
{
    bool status = true;

    UsbProtection& usbProtection = UsbProtection::GetInstance();

//    if (usbProtection.isFUSB251Ready()) {
//        slog.print("FUSB251=OK");
//    } else {
//        slog.print("FUSB251=FAIL");
//        status = false;
//    }

    if (usbProtection.isSTUSB4500Present()) {
        slog.print("STUSB4500=OK");
    } else {
        slog.print("STUSB4500=FAIL");
        status = false;
    }

    return status;
}

void USB::cmdSetDAM(int argc, const char** argv)
{
    if (argc == 1) {
        CLIConsole_print("%d\n", UsbProtection::GetInstance().isDAMLinesEnabled());
        return;
    }

    if (argc == 2) {
        if (strcmp(argv[1], "0") == 0) {
            UsbProtection::GetInstance().enableDAMLines(false);
            CLIConsole_print("OFF\n");
            return;
        }

        if (strcmp(argv[1], "1") == 0) {
            UsbProtection::GetInstance().enableDAMLines(true);
            CLIConsole_print("ON\n");
            return;
        }
    }

    CLIConsole_print("invalid parameters\n");
}

static void printSTUSB4500Status(const Hardware::STUSB4500::Status& status)
{
    CLIConsole_print("STUSB4500 status:\n");

    CLIConsole_print("  PORT_STATUS_0             : 0x%02X\n", status.portStatus0.d8);
    CLIConsole_print("    ATTACH_TRANS            : %u\n", status.portStatus0.b.ATTACH_TRANS);
    CLIConsole_print("\n");
    CLIConsole_print("  PORT_STATUS_1             : 0x%02X\n", status.portStatus1.d8);
    CLIConsole_print("    ATTACH                  : %u\n", status.portStatus1.b.ATTACH);
    CLIConsole_print("    DATA_MODE               : %u\n", status.portStatus1.b.DATA_MODE);
    CLIConsole_print("    POWER_MODE              : %u\n", status.portStatus1.b.POWER_MODE);
    CLIConsole_print("    ATTACHED_DEVICE         : %u\n", status.portStatus1.b.ATTACHED_DEVICE);
    CLIConsole_print("\n");
    CLIConsole_print("  TYPEC_MONITORING_STATUS_0 : 0x%02X\n", status.typecMonitoringStatus0.d8);
    CLIConsole_print("    VBUS_VALID_SNK_TRANS    : %u\n", status.typecMonitoringStatus0.b.VBUS_VALID_SNK_TRANS);
    CLIConsole_print("    VBUS_VSAFE0V_TRANS      : %u\n", status.typecMonitoringStatus0.b.VBUS_VSAFE0V_TRANS);
    CLIConsole_print("    VBUS_READY_TRANS        : %u\n", status.typecMonitoringStatus0.b.VBUS_READY_TRANS);
    CLIConsole_print("    VBUS_LOW_STATUS         : %u\n", status.typecMonitoringStatus0.b.VBUS_LOW_STATUS);
    CLIConsole_print("    VBUS_HIGH_STATUS        : %u\n", status.typecMonitoringStatus0.b.VBUS_HIGH_STATUS);
    CLIConsole_print("\n");
    CLIConsole_print("  TYPEC_MONITORING_STATUS_1 : 0x%02X\n", status.typecMonitoringStatus1.d8);
    CLIConsole_print("    VBUS_VALID_SNK          : %u\n", status.typecMonitoringStatus1.b.VBUS_VALID_SNK);
    CLIConsole_print("    VBUS_VSAFE0V            : %u\n", status.typecMonitoringStatus1.b.VBUS_VSAFE0V);
    CLIConsole_print("    VBUS_READY              : %u\n", status.typecMonitoringStatus1.b.VBUS_READY);
    CLIConsole_print("\n");
    CLIConsole_print("  CC_HW_FAULT_STATUS_0      : 0x%02X\n", status.ccHWFaultStatus0.d8);
    CLIConsole_print("    VPU_VALID_TRANS         : %u\n", status.ccHWFaultStatus0.b.VPU_VALID_TRANS);
    CLIConsole_print("    VPU_OVP_FAULT_TRANS     : %u\n", status.ccHWFaultStatus0.b.VPU_OVP_FAULT_TRANS);
    CLIConsole_print("\n");
    CLIConsole_print("  CC_HW_FAULT_STATUS_1      : 0x%02X\n", status.ccHWFaultStatus1.d8);
    CLIConsole_print("    VBUS_DISCH_FAULT        : %u\n", status.ccHWFaultStatus1.b.VBUS_DISCH_FAULT);
    CLIConsole_print("    VPU_VALID               : %u\n", status.ccHWFaultStatus1.b.VPU_VALID);
    CLIConsole_print("    VPU_OVP_FAULT           : %u\n", status.ccHWFaultStatus1.b.VPU_OVP_FAULT);
    CLIConsole_print("\n");
    CLIConsole_print("  CC_STATUS                 : 0x%02X\n", status.ccStatus.d8);
    CLIConsole_print("    CC1_STATE               : %u\n", status.ccStatus.b.CC1_STATE);
    CLIConsole_print("    CC2_STATE               : %u\n", status.ccStatus.b.CC2_STATE);
    CLIConsole_print("    CONNECT_RESULT          : %u\n", status.ccStatus.b.CONNECT_RESULT);
    CLIConsole_print("    LOOKING4CONNECTION      : %u\n", status.ccStatus.b.LOOKING4CONNECTION);
    CLIConsole_print("\n");
    CLIConsole_print("  PRT_STATUS                : 0x%02X\n", status.prtStatus.d8);
    CLIConsole_print("    PRL_HW_RST_RECEIVED     : %u\n", status.prtStatus.b.PRL_HW_RST_RECEIVED);
    CLIConsole_print("    PRL_MSG_RECEIVED        : %u\n", status.prtStatus.b.PRL_MSG_RECEIVED);
    CLIConsole_print("    PRT_BIST_RECEIVED       : %u\n", status.prtStatus.b.PRT_BIST_RECEIVED);
}

void USB::cmdShowStatus()
{
    UsbProtection& usbProtection = UsbProtection::GetInstance();

    Hardware::STUSB4500::Status status{};
    if (!usbProtection.getStatus(status)) {
        CLIConsole_print("failed to read STUSB4500 status\n");
        return;
    }

    printSTUSB4500Status(status);
}

void USB::cmdLog()
{
    LOG_INFO("test string fot testing the DAM mode\n");
}

void USB::cmdShowUsbFaultLineLevel()
{
    // isUsbFault() returns 'true' when the line is asserted (active-low).
    // Keep CLI semantics ("1" = fault present) consistent with that.
    CLIConsole_print("%d\n", UsbProtection::GetInstance().isUsbFault() ? 1 : 0);
}

void USB::cmdShowStatusOnLCD()
{
    UsbProtection& usbProtection = UsbProtection::GetInstance();

    Driver::SwTimer timerUpdate(500);
    Driver::SwTimer timerExit(SW_TIMER_MINUTES(1));

    while (!timerExit.expired()) {
        if (timerUpdate.check()) {
            Hardware::STUSB4500::Status status{};
            if (!usbProtection.getStatus(status)) {
                CLIConsole_print("failed to read STUSB4500 status\n");
                continue;
            }

            if (status.portStatus1.b.ATTACHED_DEVICE == Hardware::STUSB4500::PortStatusAttachedDevice::SNK_ATT) {
                mLCD.setColor(Interface::ILcd::Color::GREEN);
                LOG_INFO("SNK_ATT\n");
            } else if (status.portStatus1.b.ATTACHED_DEVICE == Hardware::STUSB4500::PortStatusAttachedDevice::DBG_ATT) {
                mLCD.setColor(Interface::ILcd::Color::BLUE);
                LOG_INFO("DBG_ATT\n");
            } else if (status.portStatus1.b.ATTACHED_DEVICE == Hardware::STUSB4500::PortStatusAttachedDevice::NONE_ATT) {
                mLCD.setColor(Interface::ILcd::Color::BLACK);
                LOG_INFO("NONE_ATT\n");
            } else {
                mLCD.setColor(Interface::ILcd::Color::RED);
                LOG_INFO("ERROR\n");
            }
        }
    }
}

void USB::checkCommand(CLIInput_t* input)
{
    int          argc = CLIInput_getArgC(input);
    const char** argv = CLIInput_getArgV(input);

    if (strcmp(argv[0], "dam") == 0) {
        cmdSetDAM(argc, argv);
    } else if (strcmp(argv[0], "status") == 0) {
        cmdShowStatus();
    } else if (strcmp(argv[0], "log") == 0) {
        cmdLog();
    } else if (strcmp(argv[0], "fault") == 0) {
        cmdShowUsbFaultLineLevel();
    } else if (strcmp(argv[0], "status2lcd") == 0) {
        cmdShowStatusOnLCD();
    } else {
        if (argc != 0) {
            CLIConsole_print("unknown command\n");
        }
    }
}

}   // namespace ProdTest
