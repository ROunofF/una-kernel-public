/**
 ******************************************************************************
 * @file    AirohaBootCheck.cpp
 * @brief   Checks that the AG3335M GPS chip responds after a reset, and
 *          whether its firmware is programmed.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Airoha.BootCheck"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "AG3335/AirohaBootCheck.hpp"

#include "OS.hpp"

#include <cstring>

namespace Airoha
{

bool BootCheck::testComm(bool &inBootMode)
{
    /* In this test we check if GPS chip communicates over UART.
     * We expect that the chip is not programmed.
     * In this case, UART baudrate is 115200.
     * Make hard reset.
     * Expected response if chip in bootloader mode:
     * %QBJS113+49
     *
     * F1: 0000 0000
     * L0: 5006 0000 [0001]
     * 00: 1012 0000
     * U0: 0000 0001 [0000]
     * T0: 0000 00B2
     * Boot failed, reset ...
     *
     * F1: 0000 0000
     * L0: 5006 0000 [0001]
     * 00: 1012 0000
     * U0: 0000 0001 [0000]
     * T0: 0000 00B2
     * System halt!
     *
     *
     * or if chip in normal mode:
     *
     * F1: 0000 0000
     *
     * V0: 0000 0000 [0001]
     *
     * 00: 0000 0000
     *
     * U0: 0000 0001 [0000]
     *
     * T0: 0000 00B2
     *
     * Leaving the BROM
     *
     *
     * Test passes if the response contains "Boot failed" (no firmware) or
     * "Leaving the BROM" (firmware programmed).
     */

    LOG_INFO("Start communication test\n");

    inBootMode = false;
    static uint8_t buffer[512] {};

    // Clear buffer
    mUart.flushRx();

    // Make hard reset
    mPwr.set(false);
    OS::Delay(500);
    mPwr.set(true);

    // The report can arrive in several pieces, so keep reading until it
    // contains a marker, the chip stays silent for a whole receive timeout,
    // or the buffer is full. The last byte stays 0 to end the string.
    const char *text = reinterpret_cast<const char*>(buffer);
    bool bootFailed = false;
    bool leftBrom = false;
    uint32_t br = 0;
    while (br < sizeof(buffer) - 1) {
        uint32_t n = uartRx(buffer + br, sizeof(buffer) - 1 - br);
        if (n == 0) {
            break;
        }
        br += n;

        bootFailed = strstr(text, "Boot failed") != nullptr;
        leftBrom = strstr(text, "Leaving the BROM") != nullptr;
        if (bootFailed || leftBrom) {
            break;
        }
    }

    mPwr.set(false);

    LOG_INFO("Received %u bytes of response:\n", br);
    bool status = false;
    if (br == 0) {
        LOG_ERROR("No response from the GPS chip\n");
    } else {
        LOG_DEBUG_DUMP(buffer, br);
        LOG_DEBUG("\n[\n%s\n]\n", text);

        if (bootFailed) {
            LOG_INFO("'Boot failed' found in response\n");
            inBootMode = true;
            status = true;
        } else if (leftBrom) {
            LOG_INFO("'Leaving the BROM' found in response\n");
            status = true;
        }
    }

    return status;
}

uint32_t BootCheck::uartRx(uint8_t* data, uint32_t length)
{
    memset(data, 0, length);
    size_t br = 0;
    bool status = mUart.receive(data, length, br, skTransferTimeout);
    LOG_DEBUG("Rx %s\n", status ? "OK" : "FAIL"); LOG_DEBUG_DUMP(data, br);
    (void)status;   // Unused when LOG_DEBUG is compiled out
    return br;
}

}  // namespace Airoha
