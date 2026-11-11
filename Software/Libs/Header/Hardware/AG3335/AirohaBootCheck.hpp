/**
 ******************************************************************************
 * @file    AirohaBootCheck.hpp
 * @brief   Checks that the AG3335M GPS chip responds after a reset, and
 *          whether its firmware is programmed.
 ******************************************************************************
 */

#ifndef AIROHA_BOOT_CHECK_HPP
#define AIROHA_BOOT_CHECK_HPP

#include "Interfaces/Drivers/IGpio.hpp"
#include "Interfaces/Drivers/IUart.hpp"

namespace Airoha
{

class BootCheck {

public:

    BootCheck(Interface::IUart &uart, Interface::IGpo &pwr) :
            mUart(uart), mPwr(pwr)
    {
    }

    /**
     * @brief   Reset the chip and check its boot ROM's report on the UART.
     * @param   inBootMode: set when the chip has no firmware to boot, and
     *          stays in its boot ROM
     * @return  true if the chip responded
     */
    bool testComm(bool &inBootMode);

private:
    Interface::IUart &mUart;
    Interface::IGpo &mPwr;

    static const uint32_t skTransferTimeout = 1000;

    uint32_t uartRx(uint8_t* data, uint32_t length);
};

}  // namespace Airoha

#endif // AIROHA_BOOT_CHECK_HPP
