/**
 ******************************************************************************
 * @file    TestGps.сpp
 * @brief   Test for the GPS module.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestGPS"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/GPS/TestGPS.hpp"
#include "Instances/InstanceUART.hpp"
#include "Gpio.hpp"
#include "AG3335/AirohaBootCheck.hpp"
#include "OS.hpp"
#include "SwTimer.hpp"
#include "CLI/CLIConsole.h"
#include "Pmic.hpp"
#include "Rtc.hpp"

namespace ProdTest
{

TestGPS::TestGPS(Interface::IUart& uart,
                 Interface::IGpo&  pwr,
                 bool              resetByButton)
    : mUart(uart)
    , mPwr(pwr)
    , mAG3335M(mUart, mPwr)
    , mSuccessCounter(0)
    , mSatellites()
    , mResetByButton(resetByButton)
    , mExit(false)
{
    if (mResetByButton) {
        HWButtons& buttons = HWButtons::GetInstance();
        buttons.config(
                Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW1),
                Driver::Gpio::GetInstance(Driver::Gpio::Pin::PWR_ON_1V8_L),
                Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW3),
                Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW4));
        buttons.init();

        buttons.attachCallback(this);
    }
}

TestGPS::~TestGPS()
{
    if (mResetByButton) {
        HWButtons& buttons = HWButtons::GetInstance();
        buttons.detachCallback(this);
    }
}

bool TestGPS::run(StringLogger& slog, CLIInput_t* input)
{
    mExit = false;

    if (!testCommunication(slog)) {
        return false;
    }

    LOG_INFO("Test Satellite...\n");

    mPwr.init(Interface::IGpo::Type::PP, false, false);
    mUart.init();

    mAG3335M.init();
    mAG3335M.setRawDataCallback(this);
    mAG3335M.setNmeaCallback(this);

    bool ok = false;
    if (mAG3335M.powerOn(5000) && mAG3335M.start(Airoha::StartType::COLD, 5000)) {
        ok = waitForSatellites(input);
    } else {
        LOG_ERROR("Failed to start the GPS\n");
    }

    mAG3335M.powerOff(2000);
    mAG3335M.setNmeaCallback(nullptr);
    mAG3335M.setRawDataCallback(nullptr);
    mAG3335M.deinit();

    return ok;
}

bool TestGPS::isExit()
{
    return mExit;
}

bool TestGPS::testCommunication(StringLogger& slog)
{
    mPwr.init(Interface::IGpo::Type::PP, false, false);

    mUart.init();
    mUart.setBuffer(nullptr, 0);
    mUart.setBaudrate(115200);
    mUart.startReception();

    Airoha::BootCheck bootCheck(mUart, mPwr);
    bool inBootMode = false;
    bool ok         = bootCheck.testComm(inBootMode);

    if (!ok) {
        LOG_ERROR("%s\n", slog.print("No communication"));
    }

    mUart.stopReception();

    mPwr.deinit();
    mUart.deinit();

    return ok;
}

bool TestGPS::waitForSatellites(CLIInput_t* input)
{
    Driver::SwTimer t(120000);
    while (!t.expired()) {
        OS::Delay(100);

        if (CLIInput_refresh(input) == true) {
            if (strcmp(CLIInput_getArgV(input)[0], "exit") == 0) {
                mExit = true;
                break;
            }

            CLIInput_reset(input);
        }

        if (mSuccessCounter > 10) {
            return true;
        }
    }

    return false;
}

void TestGPS::onRawDataRx(std::string_view rx)
{
    if (rx.substr(0, 5) == "$PAIR") {
        return; // Skip PAIR messages
    }

    LOG_DEBUG_WP("%.*s\n", rx.size() - 2, rx.data());

    CLIConsole_print("%.*s", rx.size(), rx.data()); // send as is
}

void TestGPS::onNmeaGSV(const Airoha::Nmea::GSV& cmd)
{
    if (cmd.num_satellites == 0) {
        return;
    }

    mSuccessCounter++;

    for (size_t i = 0; i < cmd.num_satellites; i++) {
        mSatellites.push_back(cmd.satellites[i]);
    }

    if (cmd.message_number == cmd.total_messages) {
        printStats(mSatellites);
        mSatellites.clear();
    }

}

void TestGPS::printStats(std::vector<Airoha::Nmea::SatelliteInfo> &sats)
{
    LOG_INFO_WP("\n");
    int idx = 1;
    for (auto &sat : sats) {
        LOG_INFO_WP("#%d: PRN=%d, SNR=%s\n", idx++, sat.prn,
                (sat.snr ? std::to_string(sat.snr).c_str() : "NA"));
    }
    LOG_INFO_WP("\n");
}

void TestGPS::onClick(Interface::IButtons::Id id)
{
    if (id == Interface::IButtons::Id::SW2) {
        // Save flag to use long press for power on
        Driver::Rtc::GetInstance().writeBackupReg(RTC_BRK_REG_POWER_OFF_FLAG, 1);

        while(true) {
            Pmic::GetInstance().powerOff();
            OS::Delay(10);
            LOG_ERROR("Still alive!\n");
        }
    }
}

}   // namespace ProdTest
