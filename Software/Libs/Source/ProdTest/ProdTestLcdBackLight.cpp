/**
 ******************************************************************************
 * @file    ProdTestLCD.сpp
 * @brief   Production test for the LCD.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.LcdBacklight"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"


#include "ProdTest/ProdTestLcdBackLight.hpp"
#include "CLI/CLIConsole.h"
#include "Gpio.hpp"
#include "OS.hpp"

namespace ProdTest
{

LcdBackLight::LcdBackLight(const std::string& id)
    : Interface::IProdTest(id)
    , mBacklight(nullptr)
{}

bool LcdBackLight::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test LCD Backlight...\n");

    Backlight bl(Driver::Gpio::GetInstance( Driver::Gpio::Pin::BACKLIGHT_ON));
    if (!bl.init()) {
        LOG_ERROR("%s\n", slog.print("Failed to init the Backlight"));
        return false;
    }

    mBacklight = &bl;

    CLIConsole_print(CLI_UTIL_PROMPT);

    while (true) {
        OS::Delay(10);

        if (CLIInput_refresh(input) == true) {
            if (strcmp(CLIInput_getArgV(input)[0], "exit") == 0) {
                break;
            }

            checkCommand(slog, input);

            CLIInput_reset(input);
            CLIConsole_print(CLI_UTIL_PROMPT);
        }
    }

    bl.deinit();

    return true;
}

void LcdBackLight::cmdOn(StringLogger& slog)
{
    slog.print("ON");

    mBacklight->on();
}

void LcdBackLight::cmdOff(StringLogger& slog)
{
    slog.print("OFF");

    mBacklight->off();
}

void LcdBackLight::checkCommand(StringLogger& slog, CLIInput_t* input)
{
    int          argc = CLIInput_getArgC(input);
    const char** argv = CLIInput_getArgV(input);

    if (strcmp(argv[0], "ON") == 0) {
        cmdOn(slog);
    } else if (strcmp(argv[0], "OFF") == 0) {
        cmdOff(slog);
    } else {
        if (argc != 0) {
            CLIConsole_print("unknown command\n");
        }
    }
}

}   // namespace ProdTest
