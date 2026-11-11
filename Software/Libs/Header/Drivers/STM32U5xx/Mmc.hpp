/**
 ******************************************************************************
 * @file    Mmc.hpp
 * @brief   MMC driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __DRIVER_MMC_HPP
#define __DRIVER_MMC_HPP

#include <cstdint>
#include <cstdbool>

#include "IFlash.hpp"

#include "main.h"

namespace Driver
{

/**
 * @brief   MMC driver.
 */
class Mmc : public virtual Interface::IFlash {

public:

    /**
     * @brief   Available ports.
     */
    enum class Port : uint8_t {
        PORT_1 = 0,

        PORT_NUM    // Not a valid parameter
    };

    /**
     * @brief   Get driver instance.
     * @param   port: target port.
     * @retval  Driver instance.
     */
    static Mmc& GetInstance(Port port);

    /**
     * @brief   Initialization of the MMC instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool init() override;

    /**
     * @brief   Deinitialization of the MMC instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool deinit() override;

    /**
     * @brief   Get block size in bytes.
     * @retval  Block size in bytes.
     */
    virtual uint32_t blockGetSize() override;

    /**
     * @brief   Get number of blocks.
     * @retval  Number of blocks.
     */
    virtual uint32_t blockGetCount() override;

    /**
     * @brief   Erases the specified block.
     * @param   blockIndex: block number
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool blockErase(uint32_t blockIndex, bool blocking = true) override;

    /**
     * @brief   Get sector size in bytes.
     * @retval  Sector size in bytes.
     */
    virtual uint32_t sectorGetSize() override;

    /**
     * @brief   Get number of sectors.
     * @retval  Number of sectors.
     */
    virtual uint32_t sectorGetCount() override;

    /**
     * @brief   Erases the specified sector.
     * @param   sectorIndex: sector number
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool sectorErase(uint32_t sectorIndex, bool blocking = true)
            override;

    /**
     * @brief   Write an amount of data to the memory.
     * @param   adr: Memory address.
     * @param   pData: Pointer to data buffer.
     * @param   size: Size of the data in bytes.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool memWrite(uint32_t adr, const uint8_t *pData, uint32_t size,
            bool blocking = true) override;

    /**
     * @brief   Read an amount of data from the memory.
     * @param   adr: Memory address.
     * @param   pData: Pointer to data buffer.
     * @param   size: Amount of data to be read.
     * @param   blocking: 'true' blocking mode, 'false' interrupt/DMA mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool memRead(uint32_t adr, uint8_t *pData, uint32_t size,
            bool blocking = true) override;

    /**
     * @brief   Set transfer complete interrupt handler.
     * @param   pHandler: interrupt handler IMmcCpltHandler.
     */
    virtual void setCpltHandler(Interface::IFlash::CpltHandler *pHandler)
        override
    {
        mMmcCpltHandler = pHandler;
    }

    // Interrupt handlers for this driver.
    // Call this method from stm32h7xx_it.c/cpp file.
    static void IrqHandlerPort1();

private:

    // Port number
    Port mPort { };

    // HAL MMC object
    MMC_HandleTypeDef mMmcHandle { };

    // Indicates that the MMC port is initialized
    bool mIsInitialized { };

    // Transfer timeout in ms. Upper bound for waiting a live-but-busy card to
    // return to the transfer state (internal programming after a write/erase).
    static const uint32_t skTransferTimeout = 1000;

    // SEND_STATUS (CMD13) command-response timeout in ms. CMD13 returns an R1
    // response, so a live card answers within microseconds regardless of its
    // internal state; a stuck/absent bus is detected within this bound instead
    // of the 5 s SDMMC_CMDTIMEOUT default.
    static const uint32_t skStatusCmdTimeout = 100;

    // Idle time between consecutive status polls in us.
    static const uint32_t skPollIntervalUs = 1000;

    // Transfer complete interrupt handler
    Interface::IFlash::CpltHandler *mMmcCpltHandler { };

    /**
     * @brief   Constructor.
     */
    Mmc() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Mmc()
    {
        deinit();
    }

    /**
     * @brief   Find instance by MMC handle.
     * @param   hmmc: HAL MMC Handle
     * @retval  Driver instance.
     */
    static Mmc& FindInstance(MMC_HandleTypeDef *mmcHandle);

    /**
     * @brief   Initialization of the MMCx instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool MMCx_Init(MMC_TypeDef *pInstance);

    /**
     * @brief   Deinitialization of the MMCx instance.
     */
    void MMCx_DeInit();

    /**
     * @brief   Switch the card to High Speed (52 MHz) timing if it advertises
     *          support, keeping the eMMC in an in-spec operating mode instead
     *          of the out-of-spec default-speed timing at the full SDMMC clock.
     *          Stays on default speed if the card has no HS support.
     */
    void trySwitchHighSpeed();

    // HAL callbacks
    static void MspInitCallback(MMC_HandleTypeDef *mmcHandle);
    static void MspDeinitCallback(MMC_HandleTypeDef *mmcHandle);
    static void TxRxCpltCallback(MMC_HandleTypeDef *mmcHandle);
    static void ErrorCallback(MMC_HandleTypeDef *mmcHandle);

    /**
     * @brief   Read the card state via SEND_STATUS (CMD13) using the LL layer
     *          with a short command-response timeout.
     * @param   cmdTimeoutMs: command-response timeout in ms.
     * @param   err: out, SDMMC error code (SDMMC_ERROR_NONE on success).
     * @retval  Card state; HAL_MMC_CARD_ERROR when the command failed.
     */
    HAL_MMC_CardStateTypeDef getCardStateFast(uint32_t cmdTimeoutMs,
            uint32_t &err);

    bool waitForState(HAL_MMC_CardStateTypeDef state, uint32_t timeout);

};

} /* namespace Driver */

#endif /* __DRIVER_MMC_HPP */
