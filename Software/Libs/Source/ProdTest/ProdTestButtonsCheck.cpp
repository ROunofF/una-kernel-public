/**
 ******************************************************************************
 * @file    ProdTestButtonsCheck.сpp
 * @brief   Production test for the Buttons.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Buttons"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestButtonsCheck.hpp"
#include "CLI/CLIConsole.h"
#include "HWButtons.hpp"
#include "OS.hpp"

namespace ProdTest
{

ButtonsCheck::ButtonsCheck(const std::string& id)
    : Interface::IProdTest(id)
    , mButtonsHistory(0)
    , mButtonsState(0)
    , mSW2Counter(0)
    , mUpdateButtonsStateTimer(400)
    , mTimeout(SW_TIMER_SECONDS(10))
{}

bool ButtonsCheck::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test Buttons Check...\n");

    HWButtons& buttons = HWButtons::GetInstance();
    buttons.config(
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW1),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::PWR_ON_1V8_L),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW3),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW4));
    buttons.init();

    ProdTest::TestLCD test;
    if (!test.init(slog)) {
        return false;
    }

    test.setColor(Interface::ILcd::Color::BLACK);

    mTimeout.reset();

    CLIConsole_print(CLI_UTIL_PROMPT);

    while (true) {
        OS::Delay(50);

        fixButtonsState();
        updateButtonsState();
        refreshButtonsState(test);

        if (mTimeout.oneshot()) {
            break;
        }

        if (CLIInput_refresh(input) == true) {
            if (strcmp(CLIInput_getArgV(input)[0], "exit") == 0) {
                break;
            }

            checkCommand(slog, input);

            CLIInput_reset(input);
            CLIConsole_print(CLI_UTIL_PROMPT);
        }
    }

    if (mButtonsHistory == 0x0F) {
        return true;
    } else {
        char    msg[64] = {};
        size_t  pos     = 0;
        uint8_t mask    = 1;

        for (uint8_t idx = 0; idx < 4; ++idx) {
            if (!(mButtonsHistory & mask)) {
                pos += snprintf(msg + pos,
                                sizeof(msg) - pos,
                                "SW%d ",
                                static_cast<int>(idx + 1));
            }

            mask <<= 1;
        }

        LOG_INFO("%s\n", slog.print("%snot pressed", msg));

        return false;
    }
}

void ButtonsCheck::fixButtonsState()
{
    mPrevButtonsState = mButtonsState;
}

void ButtonsCheck::refreshButtonsState(ProdTest::TestLCD& test)
{
    if (mPrevButtonsState == mButtonsState) {
        return;
    }

    test.fillRect(0,   0, 49, 49, mButtonsState & 0x01 ? Interface::ILcd::Color::GREEN : Interface::ILcd::Color::BLACK);
    test.fillRect(50,  0, 49, 49, mButtonsState & 0x02 ? Interface::ILcd::Color::GREEN : Interface::ILcd::Color::BLACK);
    test.fillRect(0,  50, 49, 49, mButtonsState & 0x04 ? Interface::ILcd::Color::GREEN : Interface::ILcd::Color::BLACK);
    test.fillRect(50, 50, 49, 49, mButtonsState & 0x08 ? Interface::ILcd::Color::GREEN : Interface::ILcd::Color::BLACK);
    test.paint();
}

void ButtonsCheck::setButtonState(bool state, uint16_t pos)
{
    const uint16_t mask = (1u << pos);

    if (state) {
        mButtonsState   |= mask;
        mButtonsHistory |= mask;
        mTimeout.reset();
    } else {
        mButtonsState &= static_cast<uint16_t>(~mask);
    }
}

void ButtonsCheck::updateButtonsState()
{
    HWButtons& buttons = HWButtons::GetInstance();

    setButtonState(buttons.get(Interface::IButtons::Id::SW1), 0);

    // Without a connected battery, this button may not be powered.
    // The PMIC tests the battery from time to time, so a voltage
    // appears on VBAT (300ms on / 300ms off).
    // This range needs to be covered.
    const uint8_t sw2 = (buttons.get(Interface::IButtons::Id::SW2) << 1);
    if (sw2) {
        setButtonState(true, 1);
        mUpdateButtonsStateTimer.reset();
    } else {
        if (mUpdateButtonsStateTimer.check()) {
            setButtonState(false, 1);
        }
    }

    setButtonState(buttons.get(Interface::IButtons::Id::SW3), 2);
    setButtonState(buttons.get(Interface::IButtons::Id::SW4), 3);
}

void ButtonsCheck::cmdSwCheck() const
{
    CLIConsole_print("%d\n", (int)mButtonsState);
}

void ButtonsCheck::checkCommand(StringLogger& slog, CLIInput_t* input)
{
    (void) slog;

    int          argc = CLIInput_getArgC(input);
    const char** argv = CLIInput_getArgV(input);

    if (strcmp(argv[0], "SW_CHECK") == 0) {
        cmdSwCheck();
        mTimeout.reset();
    } else {
        if (argc != 0) {
            CLIConsole_print("unknown command\n");
        }
    }
}

}   // namespace ProdTest
