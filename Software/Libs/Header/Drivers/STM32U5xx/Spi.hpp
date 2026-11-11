/**
 ******************************************************************************
 * @file    Spi.hpp
 * @brief   SPI driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef DRIVER_SPI_HPP
#define DRIVER_SPI_HPP

#include <cstdint>
#include <cstdbool>

#include "ISpi.hpp"

#include "main.h"

namespace Driver
{

/**
 * @brief   SPI driver.
 */
class Spi : public virtual Interface::ISpi
{

public:

    /**
     * @brief   Available ports.
     */
    enum class Port : uint8_t {
        PORT_1 = 0,
        PORT_2,
        PORT_3,

        PORT_NUM    // Not a valid parameter
    };

    /**
     * @brief   Get driver instance.
     * @param   port: target port.
     * @retval  Driver instance.
     */
    static Spi& GetInstance(Port port);

    /**
     * @brief   Initialization of the SPI instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() override;

    /**
     * @brief   Deinitialization of the SPI instance.
     */
    virtual void deinit() override;

    /**
     * @brief   Changing the mode of the operation of the SPI port.
     * @param   mode: operation @Mode_t.
     */
    virtual void setMode(Mode mode) override;

    /**
     * @brief   Transmits in master mode an amount of data.
     * @param   pData: Pointer to the array of data to write.
     * @param   size: Size of the data in bytes.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool transmit(const uint8_t *pData, uint16_t size, bool blocking =
            true) override;

    /**
     * @brief   Receives in master mode an amount of data.
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool receive(uint8_t *pData, uint16_t size, bool blocking = true) override;

    /**
     * @brief   Transmit and Receive in master mode an amount of data.
     * @param   pTxData: pointer to transmission data buffer.
     * @param   pRxData: pointer to reception data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool transfer(uint8_t *pTxData, uint8_t *pRxData, uint16_t size,
            bool blocking = true) override;

    /**
     * @brief   Recover the peripheral via a full DeInit/Init cycle. This is
     *          the only IRQ-independent way to force the HAL state machine
     *          back to READY after a transfer (DMA) was abandoned on timeout.
     *          The configured SPI mode is preserved across the cycle.
     * @retval  'true' - success, 'false' - otherwise.
     */
    virtual bool recover() override;

    /**
     * @brief   Set transfer complete interrupt handler.
     * @param   pHandler: interrupt handler ISpi::CpltHandler.
     */
    virtual void setCpltHandler(Interface::ISpi::CpltHandler *pHandler) override
    {
        mSpiCpltHandler = pHandler;
    }

    // Interrupt handlers for this driver.
    // Call these from the matching ISRs in stm32u5xx_it.c.
    static void IrqHandlerPort1();
    static void IrqHandlerPort2();
    static void IrqHandlerPort3();

    static void IrqDmaCh0HandlerPort1();
    static void IrqDmaCh1HandlerPort1();
    static void IrqDmaCh2HandlerPort2();
    static void IrqDmaCh3HandlerPort2();
    static void IrqDmaCh4HandlerPort3();
    static void IrqDmaCh5HandlerPort3();


private:

    // Port number
    Port mPort { };

    // HAL SPI object
    SPI_HandleTypeDef mSpiHandle { };

    // Indicates that the SPI port is initialized
    bool mIsInitialized = false;

    // Transfer timeout in ms
    static const uint32_t skTransferTimeout = 500;

    // Transfer complete interrupt handler
    Interface::ISpi::CpltHandler *mSpiCpltHandler = nullptr;

    // Current SPI mode, kept so recover() can re-apply it after a re-init
    Mode mMode = Mode::kMode0;

    /**
     * @brief   Constructor.
     */
    Spi() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Spi()
    {
        deinit();
    }

    /**
     * @brief   Find instance by SPI handle.
     * @param   hspi: HAL SPI Handle
     * @retval  Driver instance.
     */
    static Spi& FindInstance(SPI_HandleTypeDef *spiHandle);

    /**
     * @brief   Initialization of the SPIx instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool SPIx_Init(SPI_TypeDef *pInstance);

    /**
     * @brief   Deinitialization of the SPIx instance.
     */
    void SPIx_DeInit();

    /**
     * @brief   Disable this port's SPI and DMA interrupt lines in the NVIC.
     */
    void nvicDisable();

    // HAL callbacks
    static void MspInitCallback(SPI_HandleTypeDef *spiHandle);
    static void MspDeinitCallback(SPI_HandleTypeDef *spiHandle);
    static void TxRxCpltCallback(SPI_HandleTypeDef *spiHandle);
    static void ErrorCallback(SPI_HandleTypeDef *spiHandle);

};

} // namespace Driver

#endif // DRIVER_SPI_HPP
