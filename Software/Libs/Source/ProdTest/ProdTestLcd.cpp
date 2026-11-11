/**
 ******************************************************************************
 * @file    ProdTestLCD.сpp
 * @brief   Production test for the LCD.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Lcd"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"


#include "ProdTest/ProdTestLCD.hpp"
#include "CLI/CLIConsole.h"

#include <memory>

namespace ProdTest
{

Lcd::Lcd(const std::string& id)
    : Interface::IProdTest(id)
{}

bool Lcd::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test LCD...\n");

    ProdTest::TestLCD test;
    if (!test.init(slog)) {
        return false;
    }

    CLIConsole_print(CLI_UTIL_PROMPT);

    while (true) {
        OS::Delay(10);

        if (CLIInput_refresh(input) == true) {
            if (strcmp(CLIInput_getArgV(input)[0], "exit") == 0) {
                break;
            }

            checkCommand(slog, test, input);

            CLIInput_reset(input);
            CLIConsole_print(CLI_UTIL_PROMPT);
        }
    }

    test.setColor(Interface::ILcd::Color::BLACK);

    OS::Delay(10);

    return true;
}

void Lcd::setColor(StringLogger& slog, ProdTest::TestLCD& test, int argc, const char** argv)
{
    if (argc != 2) {
        LOG_INFO(CLI_INVALID_PARAM_NUMBER_MSG);
        return;
    }

    Interface::ILcd::Color color;
    std::string name(argv[1]);

    if (name == "RED") {
        color = Interface::ILcd::Color::RED;
    } else if (name == "GREEN") {
        color = Interface::ILcd::Color::GREEN;
    } else if (name == "BLUE") {
        color = Interface::ILcd::Color::BLUE;
    } else if (name == "WHITE") {
        color = Interface::ILcd::Color::WHITE;
    } else if (name == "BLACK") {
        color = Interface::ILcd::Color::BLACK;
    } else {
        LOG_INFO("%s\n", slog.print("[%s] - incorrect color name", argv[1]));
        return;
    }

    LOG_INFO("Screen is %s...'\n", name.c_str());
    slog.print("%s", name.c_str());
    test.setColor(color);
}

void Lcd::checkCommand(StringLogger& slog, ProdTest::TestLCD& test, CLIInput_t* input)
{
    int          argc = CLIInput_getArgC(input);
    const char** argv = CLIInput_getArgV(input);

    if (strcmp(argv[0], "color") == 0) {
        setColor(slog, test, argc, argv);
    } else {
        if (argc != 0) {
            CLIConsole_print("unknown command\n");
        }
    }
}

}   // namespace ProdTest
