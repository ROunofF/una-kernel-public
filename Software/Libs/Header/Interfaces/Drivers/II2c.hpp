/**
 ******************************************************************************
 * @file    II2c.hpp
 * @brief   I2C bus interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INTERFACE_I_I2C_HPP
#define INTERFACE_I_I2C_HPP

#include <cstdint>
#include <cstdbool>

namespace Interface
{

/**
 * @brief   I2C bus interface.
 */
class II2c
{

public:

    /**
     * @brief   Initialization of the I2C instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() = 0;

    /**
     * @brief   Deinitialization of the I2C instance.
     */
    virtual void deinit() = 0;

    /**
     * @brief   Transmits in master mode an amount of data.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @param   pData: Pointer to the array of data to write.
     * @param   size: Size of the data in bytes.
     * @param   blocking: 'true' blocking mode, 'false' interrupt mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool transmit(uint8_t devAddr, const uint8_t *pData, uint16_t size,
            bool blocking = true) = 0;

    /**
     * @brief   Receives in master mode an amount of data.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool receive(uint8_t devAddr, uint8_t *pData, uint16_t size,
            bool blocking = true) = 0;

    /**
     * @brief   Write an amount of data to a specific memory address/register.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @param   memAddr: Internal memory address.
     * @param   memAddrSize: Size of the memory address (1 or 2).
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be sent.
     * @param   blocking: 'true' blocking mode, 'false' interrupt mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool writeMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
            const uint8_t *pData, uint16_t size, bool blocking = true) = 0;

    /**
     * @brief   Read an amount of data from a specific memory address/register.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @param   memAddr: Internal memory address.
     * @param   memAddrSize: Size of the memory address (1 or 2).
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool readMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
            uint8_t *pData, uint16_t size, bool blocking = true) = 0;

    /**
     * @brief   Checks if target device is ready for communication.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @retval  Execution status. 'true' - device ready, 'false' - device
     *          not responded.
     */
    virtual bool isDeviceReady(uint8_t devAddr) = 0;

    /**
     * @brief   Reports whether the I2C bus is currently busy (BUSY flag
     *          asserted by the peripheral). Used to detect a stuck SDA
     *          held low by a slave whose power is being removed.
     * @retval  'true' - bus is busy / SDA held low, 'false' - bus is idle.
     */
    virtual bool isBusBusy() = 0;

    /**
     * @brief   Recover the peripheral after a failed or timed-out transfer.
     *          Brings the driver back to a known-good idle state so the next
     *          transfer can start. Concrete drivers override this; the
     *          default does nothing (suitable for stubs/mocks).
     * @retval  'true' - recovered, 'false' - not supported / failed.
     */
    virtual bool recover() { return false; }

    /**
     * @brief   I2C transfer complete interrupt handler interface.
     */
    class CpltHandler
    {
    public:
        /**
         * @brief   Transfer complete interrupt handler
         * @param   pI2c: pointer to the II2c instance.
         * @param   status: 'true' - success, 'false' - otherwise.
         */
        virtual void transferCplt(Interface::II2c *pI2c, bool status) = 0;
    };

    /**
     * @brief   Set transfer complete interrupt handler.
     * @param   pHandler: interrupt handler II2c::CpltHandler.
     */
    virtual void setCpltHandler(Interface::II2c::CpltHandler *pHandler) = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~II2c() = default;

};

} // namespace Interface

#endif // INTERFACE_I_I2C_HPP
