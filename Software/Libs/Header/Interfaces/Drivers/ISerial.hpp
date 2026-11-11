/**
 ******************************************************************************
 * @file    ISerial.hpp
 * @brief   Serial bus interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_SERIAL_HPP
#define __INTERFACE_I_SERIAL_HPP

#include <stdint.h>
#include <stdbool.h>

namespace Interface
{

/**
 * @brief   Serial bus interface.
 */
class ISerial
{

public:

    /**
     * @brief   Transmit data in blocking mode.
     * @param   pData: pointer to transmit buffer.
     * @param   size: data size.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    virtual bool transmit(const uint8_t *pData, uint32_t size, bool blocking =
            true) = 0;

    /**
     * @brief   Receives data.
     * @param   pData: Pointer to data buffer.
     * @param   size: The maximum amount of data to receive.
     * @param   br: the number of bytes actually received
     * @param   timeout: timeout waiting for bytes to be available.
     * @retval  'true' - received at least 1 byte, 'false' - no data received
     *          or any error.
     */
    virtual bool receive(uint8_t *pData, uint32_t size, uint32_t &br,
            uint32_t timeout = 0) = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~ISerial() = default;

};

} /* namespace Interface */

#endif /* __INTERFACE_I_SERIAL_HPP */
