/**
 ******************************************************************************
 * @file    Mmc.cpp
 * @brief   MMC driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Mmc.hpp"

#define LOG_MODULE_PRX      "Mmc"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>
#include "System.hpp"
#include "UsTimer.hpp"

namespace Driver
{

#define LOG_HAL_ERR_MSG(port, msg, err) \
        LOG_ERROR("PORT_%u: %s failed. Err %d\n", static_cast<uint8_t>(port) + 1, msg, err);

#define LOG_MMC_ERR_MSG(port, msg, err) \
    do {    \
        uint32_t mmcErr = HAL_MMC_GetError(&GetInstance(port).mMmcHandle); \
        LOG_ERROR("PORT_%u: %s failed. HAL Err %d, MMC err 0x%08X\n", static_cast<uint8_t>(port) + 1, msg, err, mmcErr); \
        (void)mmcErr; \
    } while (0)

Mmc& Mmc::GetInstance(Port port)
{
    assert(port < Mmc::Port::PORT_NUM);

    uint8_t n = static_cast<uint8_t>(port);
    static Mmc sInstance[static_cast<uint32_t>(Port::PORT_NUM)];
    sInstance[n].mPort = port;
    return sInstance[n];
}

bool Mmc::init()
{
    switch (mPort) {
        case Port::PORT_1:
            return MMCx_Init(SDMMC1);

        default:
            assert(false);
            break;
    }

    return false;
}

bool Mmc::deinit()
{
    MMCx_DeInit();
    return true;
}

uint32_t Mmc::blockGetSize()
{
    HAL_MMC_CardInfoTypeDef cardInfo {};
    HAL_MMC_GetCardInfo(&mMmcHandle, &cardInfo);

//    LOG_INFO("CardType: %d\n",cardInfo.CardType);
//    LOG_INFO("Class: %d\n",cardInfo.Class);
//    LOG_INFO("RelCardAdd: %d\n",cardInfo.RelCardAdd);
//    LOG_INFO("BlockNbr: %d\n",cardInfo.BlockNbr);
//    LOG_INFO("BlockSize: %d\n",cardInfo.BlockSize);
//    LOG_INFO("LogBlockNbr: %d\n",cardInfo.LogBlockNbr);
//    LOG_INFO("LogBlockSize: %d\n",cardInfo.LogBlockSize);

    return cardInfo.LogBlockSize;
}

uint32_t Mmc::blockGetCount()
{
    HAL_MMC_CardInfoTypeDef cardInfo {};
    HAL_MMC_GetCardInfo(&mMmcHandle, &cardInfo);
    return cardInfo.LogBlockNbr;
}

bool Mmc::blockErase(uint32_t blockIndex, bool blocking)
{
    uint32_t sectorsPerBlock = blockGetSize() / sectorGetSize();
    uint32_t startSector = blockIndex * sectorsPerBlock;

    for (uint32_t i = 0; i < sectorsPerBlock; i++) {
        if (sectorErase(startSector + i, blocking) == false) {
            return false;
        }
    }

    return true;
}

uint32_t Mmc::sectorGetSize()
{
    HAL_MMC_CardInfoTypeDef cardInfo {};
    HAL_MMC_GetCardInfo(&mMmcHandle, &cardInfo);

    return cardInfo.BlockSize;
}

uint32_t Mmc::sectorGetCount()
{
    HAL_MMC_CardInfoTypeDef cardInfo {};
    HAL_MMC_GetCardInfo(&mMmcHandle, &cardInfo);
    return cardInfo.BlockNbr;
}

bool Mmc::sectorErase(uint32_t sectorIndex, bool blocking)
{
    if (mIsInitialized == false){
        return false;
    }
#if 0   // Not work correctly. Erased all
    HAL_StatusTypeDef err = HAL_ERROR;

    err = HAL_MMC_Erase(&mMmcHandle, sectorIndex, sectorIndex);

    if (err != HAL_OK) {
        LOG_MMC_ERR_MSG(mPort, "HAL_MMC_Erase", err);
        return false;
    }

    return waitForState(HAL_MMC_CARD_TRANSFER, skTransferTimeout);
#else
//    uint32_t ss = sectorGetSize();
//    uint8_t dummy[ss] {};
//    uint32_t adr = sectorIndex * ss;
//    return memWrite(adr, dummy, ss, blocking);

    uint32_t sectorSize = sectorGetSize();
    uint32_t startAddr  = sectorIndex * sectorSize;
    uint32_t endAddr    = startAddr + sectorSize - 1;

    HAL_StatusTypeDef err = HAL_MMC_Erase(&mMmcHandle, startAddr, endAddr);
    if (err != HAL_OK) {
        LOG_MMC_ERR_MSG(mPort, "HAL_MMC_Erase", err);
        return false;
    }

    // Waiting for the erasing to complete
    return waitForState(HAL_MMC_CARD_TRANSFER, skTransferTimeout);
#endif
}

bool Mmc::memWrite(uint32_t adr, const uint8_t *pData, uint32_t size,
        bool blocking)
{
    assert(pData != nullptr);

    if (mIsInitialized == false) {
        return false;
    }

    if (!waitForState(HAL_MMC_CARD_TRANSFER, skTransferTimeout)) {
        return false;
    }

    HAL_StatusTypeDef err = HAL_ERROR;
    uint32_t sectorSize = sectorGetSize();

    // This driver does not support non-sector access
    if ((size % sectorSize) != 0 || (adr % sectorSize) != 0) {
        return false;
    }

    if (blocking) {
        err = HAL_MMC_WriteBlocks(&mMmcHandle, pData, (adr / sectorSize),
                (size / sectorSize), skTransferTimeout);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(mPort, "HAL_MMC_WriteBlocks", err);
        }
    } else {
        err = HAL_MMC_WriteBlocks_DMA(&mMmcHandle, pData, (adr / sectorSize),
                (size / sectorSize));
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(mPort, "HAL_MMC_WriteBlocks_DMA", err);
        }
    }

    return err == HAL_OK;
}

bool Mmc::memRead(uint32_t adr, uint8_t *pData, uint32_t size, bool blocking)
{
    assert(pData != nullptr);

    if (mIsInitialized == false) {
        return false;
    }

    if (!waitForState(HAL_MMC_CARD_TRANSFER, skTransferTimeout)) {
        return false;
    }

    HAL_StatusTypeDef err = HAL_ERROR;
    uint32_t sectorSize = sectorGetSize();

    // This driver does not support non-sector access
    if ((size % sectorSize) != 0 || (adr % sectorSize) != 0) {
        return false;
    }

    if (blocking) {
        err = HAL_MMC_ReadBlocks(&mMmcHandle, pData, (adr / sectorSize),
                (size / sectorSize), skTransferTimeout);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(mPort, "HAL_MMC_ReadBlocks", err);
        }
    } else {
        err = HAL_MMC_ReadBlocks_DMA(&mMmcHandle, pData, (adr / sectorSize),
                (size / sectorSize));
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(mPort, "HAL_MMC_ReadBlocks_DMA", err);
        }
    }

    return err == HAL_OK;
}

void Mmc::IrqHandlerPort1()
{
    HAL_MMC_IRQHandler(&GetInstance(Port::PORT_1).mMmcHandle);
}

Mmc& Mmc::FindInstance(MMC_HandleTypeDef *mmcHandle)
{
    for (uint8_t i = 0; i < static_cast<uint8_t>(Port::PORT_NUM); i++) {
        Mmc &instance = GetInstance(static_cast<Port>(i));
        if (&instance.mMmcHandle == mmcHandle) {
            return instance;
        }
    }
    assert(false);
    // Should never happen. Prevent compiler warning
    return GetInstance(Port::PORT_1);
}

bool Mmc::MMCx_Init(MMC_TypeDef *pInstance)
{
    // DWT cycle counter is the time base for the status wait: it keeps running
    // in interrupt context (USB MSC), unlike HAL_GetTick which relies on the
    // low-priority SysTick.
    UsTimer::Init();

    if (mIsInitialized){
        return true;
    }

    HAL_StatusTypeDef err = HAL_ERROR;

    mMmcHandle.Instance = pInstance;
    mMmcHandle.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
    mMmcHandle.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_ENABLE;
    mMmcHandle.Init.BusWide = SDMMC_BUS_WIDE_4B;
    mMmcHandle.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_ENABLE;
    mMmcHandle.Init.ClockDiv = 0;

    mMmcHandle.MspInitCallback = MspInitCallback;
    mMmcHandle.MspDeInitCallback = MspDeinitCallback;

    err = HAL_MMC_Init(&mMmcHandle);

    if (err != HAL_OK) {
        LOG_MMC_ERR_MSG(mPort, "HAL_MMC_Init", err);
        return false;
    }

    mMmcHandle.TxCpltCallback = TxRxCpltCallback;
    mMmcHandle.RxCpltCallback = TxRxCpltCallback;
    mMmcHandle.ErrorCallback = ErrorCallback;

    trySwitchHighSpeed();

    mIsInitialized = true;
    return true;
}

void Mmc::MMCx_DeInit()
{
    if (!mIsInitialized){
        return;
    }

    HAL_MMC_DeInit(&mMmcHandle);
    mIsInitialized = false;
}

void Mmc::trySwitchHighSpeed()
{
    // HAL_MMC_Init leaves the card in backward-compatible (DEFAULT) timing,
    // rated for <=26 MHz, while SDMMC_CK runs at the full kernel rate with
    // ClockDiv=0 - out of spec. High Speed switches BOTH the card (ExtCSD
    // HS_TIMING) and the host (CLKCR BUSSPEED) into the 52 MHz timing contract,
    // and the HAL picks a divider that keeps SDMMC_CK <=52 MHz. That is an
    // in-spec operating point at the same electrical clock, unlike merely
    // lowering the frequency which stays in the out-of-spec default timing.
    // Keep the safe default speed if the card does not advertise HS.

    // CARD_TYPE = ExtCSD byte 196 (word index 49); bit 1 = HS 52 MHz support.
    const uint32_t cardType = mMmcHandle.Ext_CSD[49] & 0xFFU;
    if ((cardType & 0x02U) == 0U) {
        LOG_WARNING("eMMC CARD_TYPE 0x%02X: no HS52, staying on default speed\n",
                cardType);
        return;
    }

    HAL_StatusTypeDef err = HAL_MMC_ConfigSpeedBusOperation(&mMmcHandle,
            SDMMC_SPEED_MODE_HIGH);
    if (err != HAL_OK) {
        LOG_MMC_ERR_MSG(mPort, "HAL_MMC_ConfigSpeedBusOperation", err);
        return;
    }

    // Host-side confirmation (card-side SWITCH_ERROR is checked in the HAL call):
    // BUSSPEED set, the HAL-computed CLKDIV, and the resulting SDMMC_CK.
    const uint32_t clkcr   = mMmcHandle.Instance->CLKCR;
    const uint32_t clkDiv  = clkcr & SDMMC_CLKCR_CLKDIV;
    const uint32_t kerClk  = HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_SDMMC);
    const uint32_t sdmmcCk = (clkDiv == 0U) ? kerClk : (kerClk / (2U * clkDiv));
    LOG_DEBUG("eMMC HS on: CARD_TYPE 0x%02X, BUSSPEED %u, CLKDIV %u, SDMMC_CK %u Hz\n",
            cardType, (clkcr & SDMMC_CLKCR_BUSSPEED) ? 1U : 0U, clkDiv, sdmmcCk);
}

void Mmc::MspInitCallback(MMC_HandleTypeDef *mmcHandle)
{
    HAL_StatusTypeDef err = HAL_ERROR;

    GPIO_InitTypeDef GPIO_InitStruct = { 0 };
    RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };
    if (mmcHandle->Instance == SDMMC1) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_SDMMC|RCC_PERIPHCLK_CLK48;
        PeriphClkInit.IclkClockSelection = RCC_CLK48CLKSOURCE_PLL1;
        PeriphClkInit.SdmmcClockSelection = RCC_SDMMCCLKSOURCE_CLK48;
        err = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_RCCEx_PeriphCLKConfig", err);
        }

        // SDMMC1 clock enable
        __HAL_RCC_SDMMC1_CLK_ENABLE();

        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();

        // SDMMC1 GPIO Configuration
        // PC8     ------> SDMMC1_D0
        // PC9     ------> SDMMC1_D1
        // PC10    ------> SDMMC1_D2
        // PC11    ------> SDMMC1_D3
        // PC12    ------> SDMMC1_CK
        // PD2     ------> SDMMC1_CMD

        uint32_t pinsSpeed = GPIO_SPEED_FREQ_HIGH;// FREQ_HIGH required for clocks > 48Mhz

        GPIO_InitStruct.Pin = SDIO_D0_Pin |
                              SDIO_D1_Pin |
                              SDIO_D2_Pin |
                              SDIO_D3_Pin |
                              SDIO_CK_Pin;

        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_PULLUP;
        GPIO_InitStruct.Speed = pinsSpeed;
        GPIO_InitStruct.Alternate = GPIO_AF12_SDMMC1;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = SDIO_CMD_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = pinsSpeed;
        GPIO_InitStruct.Alternate = GPIO_AF12_SDMMC1;
        HAL_GPIO_Init(SDIO_CMD_GPIO_Port, &GPIO_InitStruct);

        // SDMMC1 interrupt Init
        HAL_NVIC_SetPriority(SDMMC1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(SDMMC1_IRQn);
    }
}

void Mmc::MspDeinitCallback(MMC_HandleTypeDef *mmcHandle)
{
    if (mmcHandle->Instance == SDMMC1) {
        // Peripheral clock disable
        __HAL_RCC_SDMMC1_CLK_DISABLE();

        // SDMMC1 GPIO Configuration
        // PC8     ------> SDMMC1_D0
        // PC9     ------> SDMMC1_D1
        // PC10    ------> SDMMC1_D2
        // PC11    ------> SDMMC1_D3
        // PC12    ------> SDMMC1_CK
        // PD2     ------> SDMMC1_CMD

        HAL_GPIO_DeInit(GPIOC, SDIO_D0_Pin | SDIO_D1_Pin | SDIO_D2_Pin | SDIO_D3_Pin | SDIO_CK_Pin);

        HAL_GPIO_DeInit(SDIO_CMD_GPIO_Port, SDIO_CMD_Pin);

        // SDMMC1 interrupt Deinit
        HAL_NVIC_DisableIRQ(SDMMC1_IRQn);
    }
}

void Mmc::TxRxCpltCallback(MMC_HandleTypeDef *mmcHandle)
{
    Mmc &instance = FindInstance(mmcHandle);
    if (instance.mMmcCpltHandler != nullptr) {
        instance.mMmcCpltHandler->transferCplt(&instance, true);
    }
}

void Mmc::ErrorCallback(MMC_HandleTypeDef *mmcHandle)
{
    Mmc &instance = FindInstance(mmcHandle);
    if (instance.mMmcCpltHandler != nullptr) {
        instance.mMmcCpltHandler->transferCplt(&instance, false);
    }
}

HAL_MMC_CardStateTypeDef Mmc::getCardStateFast(uint32_t cmdTimeoutMs,
        uint32_t &err)
{
    // Local reimplementation of SDMMC_CmdSendStatus / HAL_MMC_GetCardState that
    // takes an explicit command-response timeout. The HAL path hardcodes
    // SDMMC_CMDTIMEOUT (5000 ms), so a wedged bus busy-spins the CPU for ~5 s
    // per probe with no yield, starving the idle task that feeds the IWDG.
    SDMMC_CmdInitTypeDef cmd { };
    cmd.Argument         = static_cast<uint32_t>(mMmcHandle.MmcCard.RelCardAdd << 16U);
    cmd.CmdIndex         = SDMMC_CMD_SEND_STATUS;
    cmd.Response         = SDMMC_RESPONSE_SHORT;
    cmd.WaitForInterrupt = SDMMC_WAIT_NO;
    cmd.CPSM             = SDMMC_CPSM_ENABLE;
    (void)SDMMC_SendCommand(mMmcHandle.Instance, &cmd);

    err = SDMMC_GetCmdResp1(mMmcHandle.Instance, SDMMC_CMD_SEND_STATUS,
            cmdTimeoutMs);
    if (err != SDMMC_ERROR_NONE) {
        return HAL_MMC_CARD_ERROR;
    }

    uint32_t resp1 = SDMMC_GetResponse(mMmcHandle.Instance, SDMMC_RESP1);
    return static_cast<HAL_MMC_CardStateTypeDef>((resp1 >> 9U) & 0x0FU);
}

bool Mmc::waitForState(HAL_MMC_CardStateTypeDef state, uint32_t timeout)
{
    // Time base is the DWT cycle counter (ISR-safe, always advancing), not
    // HAL_GetTick which is frozen while running in the USB OTG interrupt.
    const uint32_t startCyc  = UsTimer::GetTicks();
    const uint32_t budgetCyc = timeout * (SystemCoreClock / 1000U);

    for (;;) {
        uint32_t err = SDMMC_ERROR_NONE;
        HAL_MMC_CardStateTypeDef cardState = getCardStateFast(skStatusCmdTimeout,
                err);

        // A live card answers CMD13 within microseconds even while internally
        // busy, so a command failure means the bus/card is not responding:
        // fail fast (bounded by skStatusCmdTimeout) instead of spinning out the
        // whole budget. This keeps a degrading eMMC from monopolizing the CPU.
        if (err != SDMMC_ERROR_NONE) {
            LOG_ERROR("SEND_STATUS failed, err 0x%08X\n", err);
            return false;
        }

        if (cardState == state) {
            return true;
        }

        // Live but not yet in the target state (e.g. programming after a write):
        // keep polling until the transfer budget is exhausted.
        if ((uint32_t)(UsTimer::GetTicks() - startCyc) > budgetCyc) {
            LOG_ERROR("State: %u, expected: %u. Wait timeout\n", cardState,
                    state);
            return false;
        }

        UsTimer::DelayUs(skPollIntervalUs);
    }
}

} /* namespace Driver */

