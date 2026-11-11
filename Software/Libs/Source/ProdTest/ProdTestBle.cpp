/**
 ******************************************************************************
 * @file    ProdTestBLE.сpp
 * @brief   Production test for the BLE.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.BLE"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestBLE.hpp"
#include "Instances/InstanceI2C.hpp"
#include "Gpio.hpp"
#include "Spi.hpp"
#include "System.hpp"
#include "CLI/CLIConsole.h"
#include "OS.hpp"
#include "SwTimer.hpp"

#include <memory>

namespace ProdTest
{

Ble::Ble(const std::string& id) : Interface::IProdTest(id)
{}

bool Ble::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test BLE (BlueNRG2)...\n");

    // '--unpair' removes all stored bonds instead of running the pairing flow.
    const bool unpair = CLIParser_check(&p, "--unpair");

    static ::Ble::CoreBlueNRG* core;
    if (core == nullptr) {
        core = new (std::nothrow) ::Ble::CoreBlueNRG;
    }

    if (core) {
        core->config(&Driver::Spi::GetInstance(Driver::Spi::Port::PORT_1),
                     &Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR1_CS),
                     &Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR1_TX_INT0),
                     &Driver::Gpio::GetInstance(Driver::Gpio::Pin::SENSOR1_RST_L));

    }

    if (!core) {
        LOG_ERROR("%s\n", slog.print("No memory"));
        return false;
    }

    return run(slog, *core, input, unpair);
}

bool Ble::run(StringLogger& slog, ::Ble::CoreBlueNRG& core, CLIInput_t* input, bool unpair)
{
    static ::Ble::PeripheralBlueNRG* ble;

    if (ble == nullptr) {
        ble = new (std::nothrow) ::Ble::PeripheralBlueNRG(core);
    }

    if (!ble) {
        LOG_ERROR("%s\n", slog.print("No memory"));
        return false;
    }

    char fullBleName[32] {};
    snprintf(fullBleName, sizeof(fullBleName), "%s %s",
            Driver::System::GetModelNameStr(),
            Driver::System::GetDeviceIdStr());

    ble->setName(fullBleName);
    if (!ble->init()) {
        LOG_ERROR("%s\n", slog.print("Failed to init the BLE"));
        return false;
    }

    if (!ble->enable()) {
        LOG_ERROR("%s\n", slog.print("Failed to enable the BLE"));
        ble->deinit();
        return false;
    }

    bool ok = run(slog, *ble, input, unpair);

    ble->disable();
    ble->deinit();

    return ok;
}

bool Ble::run(StringLogger& slog, ::Ble::PeripheralBlueNRG& ble, CLIInput_t* input, bool unpair)
{
    if (unpair) {
        if (!ble.unpair()) {
            LOG_ERROR("%s\n", slog.print("Failed to unpair"));
            return false;
        }

        LOG_INFO("%s\n", slog.print("Unpaired: all bonds cleared"));
        return true;
    }

    if (!ble.pair()) {
        LOG_ERROR("%s\n", slog.print("Failed to pair"));
        return false;
    }

    CLIConsole_print("advertising...\n");

    LOG_INFO("Check BLE device '%s' in NRF Connect list... (timeout 20s)\n", ble.getName());

    Driver::SwTimer t(40000);
    while (!t.expired()) {
        OS::Delay(100);

        if (CLIInput_refresh(input) == true) {
            if (strcmp(CLIInput_getArgV(input)[0], "exit") == 0) {
                break;
            }

            CLIInput_reset(input);
        }
    }

    return true;
}

}   // namespace ProdTest
