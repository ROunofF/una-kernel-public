/**
 ******************************************************************************
 * @file    ISpi.hpp
 * @brief   SPI bus interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INTERFACE_I_SPI_HPP
#define INTERFACE_I_SPI_HPP

#include <cstdint>
#include <cstdbool>

namespace Interface
{

/**
 * @brief   SPI bus interface.
 */
class ISpi
{

public:

    /**
     * @brief   SPI mode
     */
    enum class Mode {
        kMode0 = 1,  ///< CPOL = 0, CPHA = 0
        kMode1,      ///< CPOL = 0, CPHA = 1
        kMode2,      ///< CPOL = 1, CPHA = 0
        kMode3       ///< CPOL = 1, CPHA = 1
    };

    /**
     * @brief   Initialization of the SPI instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() = 0;

    /**
     * @brief   Deinitialization of the SPI instance.
     */
    virtual void deinit() = 0;

    /**
     * @brief   Changing the mode of the operation of the SPI port
     * @param   mode: operation Mode_t.
     */
    virtual void setMode(Mode mode) = 0;

    /**
     * @brief   Transmits in master mode an amount of data.
     * @param   pData: Pointer to the array of data to write.
     * @param   size: Size of the data in bytes.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool transmit(const uint8_t *pData, uint16_t size, bool blocking =
            true) = 0;

    /**
     * @brief   Receives in master mode an amount of data.
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool receive(uint8_t *pData, uint16_t size,
            bool blocking = true) = 0;

    /**
     * @brief   Transmit and Receive in master mode an amount of data.
     * @param   pTxData: pointer to transmission data buffer
     * @param   pRxData: pointer to reception data buffer
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool transfer(uint8_t *pTxData, uint8_t *pRxData, uint16_t size,
            bool blocking = true) = 0;

    /**
     * @brief   Recover the peripheral after a failed or timed-out transfer.
     *          Brings the driver back to a known-good idle state so the next
     *          transfer can start. Concrete drivers override this; the
     *          default does nothing (suitable for stubs/mocks).
     * @retval  'true' - recovered, 'false' - not supported / failed.
     */
    virtual bool recover() { return false; }

    /**
     * @brief   SPI transfer complete interrupt handler interface.
     */
    class CpltHandler
    {
    public:
        /**
         * @brief   Transfer complete interrupt handler
         * @param   pSpi: pointer to the ISpi instance.
         * @param   status: 'true' - success, 'false' - otherwise.
         */
        virtual void transferCplt(Interface::ISpi *pSpi, bool status) = 0;
    };

    /**
     * @brief   Set transfer complete interrupt handler.
     * @param   pHandler: interrupt handler ISpi::CpltHandler.
     */
    virtual void setCpltHandler(Interface::ISpi::CpltHandler *pHandler) = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~ISpi() = default;

};

} // namespace Interface

#endif // INTERFACE_I_SPI_HPP
