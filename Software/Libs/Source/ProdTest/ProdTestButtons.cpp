/**
 ******************************************************************************
 * @file    ProdTestButtons.сpp
 * @brief   Production test for the Buttons.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Buttons"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestButtons.hpp"
#include "HWButtons.hpp"
#include "OS.hpp"

namespace ProdTest
{

Buttons::Buttons(const std::string& id) : Interface::IProdTest(id)
{}

bool Buttons::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test Buttons...\n");

    HWButtons& buttons = HWButtons::GetInstance();
    buttons.config(
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW1),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::PWR_ON_1V8_L),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW3),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::SW4));
    buttons.init();

    uint8_t code = 0;



    code |= (buttons.get(Interface::IButtons::Id::SW1) << 0);

    // Without a connected battery, this button may not be powered.
    // The PMIC tests the battery from time to time, so a voltage
    // appears on VBAT (300ms on / 300ms off).
    // This range needs to be covered.
    for (uint32_t i = 0; i < 100; i++) {
        uint8_t sw2 = (buttons.get(Interface::IButtons::Id::SW2) << 1);
        code |= sw2;

        if (sw2) {
            break;
        }
        OS::Delay(5);
    }

    code |= (buttons.get(Interface::IButtons::Id::SW3) << 2);
    code |= (buttons.get(Interface::IButtons::Id::SW4) << 3);

    LOG_INFO("%s\n", slog.print("SWMASK %d", static_cast<int>(code)));

    return true;
}


}   // namespace ProdTest
