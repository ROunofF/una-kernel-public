/**
 ******************************************************************************
 * @file    Spi.cpp
 * @brief   SPI driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Spi.hpp"

#define LOG_MODULE_PRX      "Spi"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include <cassert>

static DMA_HandleTypeDef handle_GPDMA1_Channel0;
static DMA_HandleTypeDef handle_GPDMA1_Channel1;
static DMA_HandleTypeDef handle_GPDMA1_Channel2;
static DMA_HandleTypeDef handle_GPDMA1_Channel3;
static DMA_HandleTypeDef handle_GPDMA1_Channel4;
static DMA_HandleTypeDef handle_GPDMA1_Channel5;

//TODO: Config speed
namespace Driver
{

#define LOG_HAL_ERR_MSG(port, msg, err) \
        LOG_ERROR("PORT_%u: %s failed. Err %d\n", static_cast<uint8_t>(port) + 1, msg, err);

#define LOG_SPI_ERR_MSG(port, msg, err) \
    do {    \
        uint32_t spiErr = HAL_SPI_GetError(&GetInstance(port).mSpiHandle); \
        LOG_ERROR("PORT_%u: %s failed. HAL Err %d, SPI err 0x%08X\n", static_cast<uint8_t>(port) + 1, msg, err, spiErr); \
    } while (0)

Spi& Spi::GetInstance(Port port)
{
    assert(port < Spi::Port::PORT_NUM);

    uint8_t n = static_cast<uint8_t>(port);
    static Spi sInstance[static_cast<uint32_t>(Port::PORT_NUM)];
    // Set the port once. Avoids a repeated write to shared state that is
    // also touched from ISR context (IRQ handlers, error log macro).
    if (sInstance[n].mPort != port) {
        sInstance[n].mPort = port;
    }
    return sInstance[n];
}

bool Spi::init()
{
    switch (mPort) {
        case Port::PORT_1:
            return SPIx_Init(SPI1);
        case Port::PORT_2:
            return SPIx_Init(SPI2);
        case Port::PORT_3:
            return SPIx_Init(SPI3);

        default:
            assert(false);
            break;
    }

    return false;
}

void Spi::deinit()
{
    SPIx_DeInit();
}

bool Spi::recover()
{
    // Nothing to recover on a port that is intentionally down.
    if (!mIsInitialized) {
        return false;
    }

    // Mask the port interrupts up front so a late completion IRQ from the
    // abandoned DMA transfer cannot run the HAL handler while DeInit/Init
    // tears the handle down. MspInit re-enables them on init().
    nvicDisable();

    // A full DeInit/Init is the only IRQ-independent way to force the HAL
    // state machine back to READY after an abandoned transfer wedged it.
    // It also resets the DMA channels and reinitialises the GPIO.
    deinit();
    if (!init()) {
        return false;
    }

    // SPIx_Init resets CPOL/CPHA to defaults, so restore the configured mode.
    setMode(mMode);
    return true;
}

void Spi::nvicDisable()
{
    switch (mPort) {
        case Port::PORT_1:
            HAL_NVIC_DisableIRQ(SPI1_IRQn);
            HAL_NVIC_DisableIRQ(GPDMA1_Channel0_IRQn);
            HAL_NVIC_DisableIRQ(GPDMA1_Channel1_IRQn);
            break;
        case Port::PORT_2:
            HAL_NVIC_DisableIRQ(SPI2_IRQn);
            HAL_NVIC_DisableIRQ(GPDMA1_Channel2_IRQn);
            HAL_NVIC_DisableIRQ(GPDMA1_Channel3_IRQn);
            break;
        case Port::PORT_3:
            HAL_NVIC_DisableIRQ(SPI3_IRQn);
            HAL_NVIC_DisableIRQ(GPDMA1_Channel4_IRQn);
            HAL_NVIC_DisableIRQ(GPDMA1_Channel5_IRQn);
            break;
        default:
            break;
    }
}

void Spi::setMode(Mode mode)
{
    mMode = mode;   // remembered so recover() can re-apply it after a re-init

    switch (mode) {
        case ISpi::Mode::kMode0: {
            mSpiHandle.Init.CLKPolarity = SPI_POLARITY_LOW;
            mSpiHandle.Init.CLKPhase = SPI_PHASE_1EDGE;
            break;
        }
        case ISpi::Mode::kMode1: {
            mSpiHandle.Init.CLKPolarity = SPI_POLARITY_LOW;
            mSpiHandle.Init.CLKPhase = SPI_PHASE_2EDGE;
            break;
        }
        case ISpi::Mode::kMode2: {
            mSpiHandle.Init.CLKPolarity = SPI_POLARITY_HIGH;
            mSpiHandle.Init.CLKPhase = SPI_PHASE_1EDGE;
            break;
        }
        case ISpi::Mode::kMode3: {
            mSpiHandle.Init.CLKPolarity = SPI_POLARITY_HIGH;
            mSpiHandle.Init.CLKPhase = SPI_PHASE_2EDGE;
            break;
        }
    }

    // SPIx CFG2 Configuration
    MODIFY_REG(mSpiHandle.Instance->CFG2, SPI_CFG2_CPHA,
            mSpiHandle.Init.CLKPhase);
    MODIFY_REG(mSpiHandle.Instance->CFG2, SPI_CFG2_CPOL,
            mSpiHandle.Init.CLKPolarity);
}

bool Spi::transmit(const uint8_t *pData, uint16_t size, bool blocking)
{
    assert(pData != nullptr);
    HAL_StatusTypeDef err = HAL_ERROR;

    if (!mIsInitialized){
        return false;
    }

    if (blocking) {
        err = HAL_SPI_Transmit(&mSpiHandle, pData, size, skTransferTimeout);
        if (err != HAL_OK) {
            LOG_SPI_ERR_MSG(mPort, "HAL_SPI_Transmit", err);
        }
    } else {
# if 0  // Not tested
        SCB_CleanDCache_by_Addr(
                (uint32_t*) (((uint32_t) pData) & ~(uint32_t) 0x1F), size + __SCB_DCACHE_LINE_SIZE);
#endif
#if 0   // IT/DMA
        err = HAL_SPI_Transmit_IT(&mSpiHandle, pData, size);
        if (err != HAL_OK) {
            LOG_SPI_ERR_MSG(mPort, "HAL_SPI_Transmit_IT", err);
        }
#else
        err = HAL_SPI_Transmit_DMA(&mSpiHandle, pData, size);
        if (err != HAL_OK) {
            LOG_SPI_ERR_MSG(mPort, "HAL_SPI_Transmit_DMA", err);
        }
#endif
    }
    return err == HAL_OK;
}

bool Spi::receive(uint8_t *pData, uint16_t size, bool blocking)
{
    assert(pData != nullptr);
    HAL_StatusTypeDef err = HAL_ERROR;

    if (!mIsInitialized){
        return false;
    }

    if (blocking) {
        err = HAL_SPI_Receive(&mSpiHandle, pData, size, skTransferTimeout);
        if (err != HAL_OK) {
            LOG_SPI_ERR_MSG(mPort, "HAL_SPI_Receive", err);
        }
    } else {
# if 0  // Not tested
        SCB_InvalidateDCache_by_Addr(
                (uint32_t*) (((uint32_t) pData) & ~(uint32_t) 0x1F), size + __SCB_DCACHE_LINE_SIZE);
#endif
#if 0   // IT/DMA
        err = HAL_SPI_Receive_IT(&mSpiHandle, pData, size);
        if (err != HAL_OK) {
            LOG_SPI_ERR_MSG(mPort, "HAL_SPI_Receive_IT", err);
        }
#else
        err = HAL_SPI_Receive_DMA(&mSpiHandle, pData, size);
        if (err != HAL_OK) {
            LOG_SPI_ERR_MSG(mPort, "HAL_SPI_Receive_DMA", err);
        }
#endif
    }

    return err == HAL_OK;
}

bool Spi::transfer(uint8_t *pTxData, uint8_t *pRxData, uint16_t size,
        bool blocking)
{
    assert(pTxData != nullptr);
    assert(pRxData != nullptr);
    HAL_StatusTypeDef err = HAL_ERROR;

    if (!mIsInitialized){
        return false;
    }

    if (blocking) {
        err = HAL_SPI_TransmitReceive(&mSpiHandle, pTxData, pRxData, size,
                skTransferTimeout);
        if (err != HAL_OK) {
            LOG_SPI_ERR_MSG(mPort, "HAL_SPI_TransmitReceive", err);
        }
    } else {
# if 0  // Not tested
        SCB_CleanDCache_by_Addr(
                (uint32_t*) (((uint32_t) pTxData) & ~(uint32_t) 0x1F),
                size + __SCB_DCACHE_LINE_SIZE);

        SCB_InvalidateDCache_by_Addr(
                (uint32_t*) (((uint32_t) pRxData) & ~(uint32_t) 0x1F),
                size + __SCB_DCACHE_LINE_SIZE);
#endif
#if 0   // IT/DMA
        err = HAL_SPI_TransmitReceive_IT(&mSpiHandle, pTxData, pRxData, size);
        if (err != HAL_OK) {
            LOG_SPI_ERR_MSG(mPort, "HAL_SPI_TransmitReceive_IT", err);
        }
#else
        err = HAL_SPI_TransmitReceive_DMA(&mSpiHandle, pTxData, pRxData, size);
        if (err != HAL_OK) {
            LOG_SPI_ERR_MSG(mPort, "HAL_SPI_TransmitReceive_DMA", err);
        }
#endif
    }
    return err == HAL_OK;
}

void Spi::IrqHandlerPort1()
{
    HAL_SPI_IRQHandler(&GetInstance(Port::PORT_1).mSpiHandle);
}

void Spi::IrqHandlerPort2()
{
    HAL_SPI_IRQHandler(&GetInstance(Port::PORT_2).mSpiHandle);
}

void Spi::IrqHandlerPort3()
{
    HAL_SPI_IRQHandler(&GetInstance(Port::PORT_3).mSpiHandle);
}

void Spi::IrqDmaCh0HandlerPort1()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel0);
}

void Spi::IrqDmaCh1HandlerPort1()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel1);
}

void Spi::IrqDmaCh2HandlerPort2()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel2);
}

void Spi::IrqDmaCh3HandlerPort2()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel3);
}

void Spi::IrqDmaCh4HandlerPort3()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel4);
}

void Spi::IrqDmaCh5HandlerPort3()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel5);
}

Spi& Spi::FindInstance(SPI_HandleTypeDef *spiHandle)
{
    for (uint8_t i = 0; i < static_cast<uint8_t>(Port::PORT_NUM); i++) {
        Spi &instance = GetInstance(static_cast<Port>(i));
        if (&instance.mSpiHandle == spiHandle) {
            return instance;
        }
    }
    assert(false);
    // Should never happen. Prevent compiler warning
    return GetInstance(Port::PORT_1);
}

bool Spi::SPIx_Init(SPI_TypeDef *pInstance)
{
    if (mIsInitialized){
        return true;
    }

    HAL_StatusTypeDef err = HAL_ERROR;


    if (pInstance == SPI1) {
        mSpiHandle.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    } else if(pInstance == SPI2) {
        mSpiHandle.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    } else if(pInstance == SPI3) {
        mSpiHandle.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;
    }


    mSpiHandle.Instance = pInstance;
    mSpiHandle.Init.Mode = SPI_MODE_MASTER;
    mSpiHandle.Init.Direction = SPI_DIRECTION_2LINES;
    mSpiHandle.Init.DataSize = SPI_DATASIZE_8BIT;
    mSpiHandle.Init.CLKPolarity = SPI_POLARITY_LOW;
    mSpiHandle.Init.CLKPhase = SPI_PHASE_1EDGE;
    mSpiHandle.Init.NSS = SPI_NSS_SOFT;
    mSpiHandle.Init.FirstBit = SPI_FIRSTBIT_MSB;
    mSpiHandle.Init.TIMode = SPI_TIMODE_DISABLE;
    mSpiHandle.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    mSpiHandle.Init.CRCPolynomial = 0x7;
    mSpiHandle.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
    mSpiHandle.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    mSpiHandle.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    mSpiHandle.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    mSpiHandle.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    mSpiHandle.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_ENABLE;
    mSpiHandle.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
    mSpiHandle.Init.IOSwap = SPI_IO_SWAP_DISABLE;
    mSpiHandle.Init.ReadyMasterManagement = SPI_RDY_MASTER_MANAGEMENT_INTERNALLY;
    mSpiHandle.Init.ReadyPolarity = SPI_RDY_POLARITY_HIGH;

    mSpiHandle.MspInitCallback = MspInitCallback;
    mSpiHandle.MspDeInitCallback = MspDeinitCallback;

    err = HAL_SPI_Init(&mSpiHandle);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_SPI_Init", err);
        return false;
    }

    // This triggers assert_param(IS_SPI_AUTONOMOUS_INSTANCE(hspi->Instance));
//    SPI_AutonomousModeConfTypeDef HAL_SPI_AutonomousMode_Cfg_Struct = {0};
//    HAL_SPI_AutonomousMode_Cfg_Struct.TriggerState = SPI_AUTO_MODE_DISABLE;
//    HAL_SPI_AutonomousMode_Cfg_Struct.TriggerSelection = SPI_GRP2_LPDMA_CH0_TCF_TRG;
//    HAL_SPI_AutonomousMode_Cfg_Struct.TriggerPolarity = SPI_TRIG_POLARITY_RISING;
//
//    err = HAL_SPIEx_SetConfigAutonomousMode(&mSpiHandle, &HAL_SPI_AutonomousMode_Cfg_Struct);
//    if (err != HAL_OK) {
//        LOG_HAL_ERR_MSG(mPort, "HAL_SPIEx_SetConfigAutonomousMode", err);
//        return false;
//    }

    mSpiHandle.TxRxCpltCallback = TxRxCpltCallback;
    mSpiHandle.TxCpltCallback = TxRxCpltCallback;
    mSpiHandle.RxCpltCallback = TxRxCpltCallback;
    mSpiHandle.ErrorCallback = ErrorCallback;

    mIsInitialized = true;
    return true;
}

void Spi::SPIx_DeInit()
{
    if (!mIsInitialized){
        return;
    }

    HAL_SPI_DeInit(&mSpiHandle);
    mIsInitialized = false;
}

void Spi::MspInitCallback(SPI_HandleTypeDef *spiHandle)
{
    HAL_StatusTypeDef err = HAL_ERROR;
    GPIO_InitTypeDef GPIO_InitStruct = { 0 };
    RCC_PeriphCLKInitTypeDef PeriphClkInit = { 0 };

    if (spiHandle->Instance == SPI1) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_SPI1;
        PeriphClkInit.Spi1ClockSelection = RCC_SPI1CLKSOURCE_SYSCLK;
        err = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_RCCEx_PeriphCLKConfig", err);
        }

        // SPI1 clock enable
        __HAL_RCC_SPI1_CLK_ENABLE();

        __HAL_RCC_GPIOG_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        // SPI1 GPIO Configuration
        // PG2     ------> SPI1_SCK
        // PG3     ------> SPI1_MISO
        // PB5     ------> SPI1_MOSI
        GPIO_InitStruct.Pin = SENSOR1_SCK_Pin | SENSOR1_MISO_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = SENSOR1_MOSI_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
        HAL_GPIO_Init(SENSOR1_MOSI_GPIO_Port, &GPIO_InitStruct);

        // SPI1 DMA Init
        __HAL_RCC_GPDMA1_CLK_ENABLE();

        // GPDMA1_REQUEST_SPI1_TX Init
        handle_GPDMA1_Channel1.Instance = GPDMA1_Channel1;
        handle_GPDMA1_Channel1.Init.Request = GPDMA1_REQUEST_SPI1_TX;
        handle_GPDMA1_Channel1.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        handle_GPDMA1_Channel1.Init.Direction = DMA_MEMORY_TO_PERIPH;
        handle_GPDMA1_Channel1.Init.SrcInc = DMA_SINC_INCREMENTED;
        handle_GPDMA1_Channel1.Init.DestInc = DMA_DINC_FIXED;
        handle_GPDMA1_Channel1.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel1.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel1.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel1.Init.SrcBurstLength = 1;
        handle_GPDMA1_Channel1.Init.DestBurstLength = 1;
        handle_GPDMA1_Channel1.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
        handle_GPDMA1_Channel1.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel1.Init.Mode = DMA_NORMAL;
        err = HAL_DMA_Init(&handle_GPDMA1_Channel1);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_Init", err);
        }

        __HAL_LINKDMA(spiHandle, hdmatx, handle_GPDMA1_Channel1);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel1, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_ConfigChannelAttributes", err);
        }

        // GPDMA1_REQUEST_SPI1_RX Init
        handle_GPDMA1_Channel0.Instance = GPDMA1_Channel0;
        handle_GPDMA1_Channel0.Init.Request = GPDMA1_REQUEST_SPI1_RX;
        handle_GPDMA1_Channel0.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        handle_GPDMA1_Channel0.Init.Direction = DMA_PERIPH_TO_MEMORY;
        handle_GPDMA1_Channel0.Init.SrcInc = DMA_SINC_FIXED;
        handle_GPDMA1_Channel0.Init.DestInc = DMA_DINC_INCREMENTED;
        handle_GPDMA1_Channel0.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel0.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel0.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel0.Init.SrcBurstLength = 1;
        handle_GPDMA1_Channel0.Init.DestBurstLength = 1;
        handle_GPDMA1_Channel0.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
        handle_GPDMA1_Channel0.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel0.Init.Mode = DMA_NORMAL;
        err = HAL_DMA_Init(&handle_GPDMA1_Channel0);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_Init", err);
        }

        __HAL_LINKDMA(spiHandle, hdmarx, handle_GPDMA1_Channel0);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel0, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_ConfigChannelAttributes", err);
        }

        // GPDMA1 interrupt Init
        HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel0_IRQn);
        HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel1_IRQn);

        // SPI1 interrupt Init
        HAL_NVIC_SetPriority(SPI1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(SPI1_IRQn);
    } else if (spiHandle->Instance == SPI2) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_SPI2;
        PeriphClkInit.Spi2ClockSelection = RCC_SPI2CLKSOURCE_SYSCLK;
        err = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_RCCEx_PeriphCLKConfig", err);
        }

        // SPI2 clock enable
        __HAL_RCC_SPI2_CLK_ENABLE();

        __HAL_RCC_GPIOC_CLK_ENABLE();
        __HAL_RCC_GPIOD_CLK_ENABLE();
        // SPI2 GPIO Configuration
        // PC2     ------> SPI2_MISO
        // PC3     ------> SPI2_MOSI
        // PD1     ------> SPI2_SCK
        GPIO_InitStruct.Pin = SENSOR2_MISO_Pin | SENSOR2_MOSI_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
        HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = SENSOR2_SCK_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF5_SPI2;
        HAL_GPIO_Init(SENSOR2_SCK_GPIO_Port, &GPIO_InitStruct);

        // SPI2 DMA Init
        __HAL_RCC_GPDMA1_CLK_ENABLE();

        // GPDMA1_REQUEST_SPI2_TX Init
        handle_GPDMA1_Channel3.Instance = GPDMA1_Channel3;
        handle_GPDMA1_Channel3.Init.Request = GPDMA1_REQUEST_SPI2_TX;
        handle_GPDMA1_Channel3.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        handle_GPDMA1_Channel3.Init.Direction = DMA_MEMORY_TO_PERIPH;
        handle_GPDMA1_Channel3.Init.SrcInc = DMA_SINC_INCREMENTED;
        handle_GPDMA1_Channel3.Init.DestInc = DMA_DINC_FIXED;
        handle_GPDMA1_Channel3.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel3.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel3.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel3.Init.SrcBurstLength = 1;
        handle_GPDMA1_Channel3.Init.DestBurstLength = 1;
        handle_GPDMA1_Channel3.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
        handle_GPDMA1_Channel3.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel3.Init.Mode = DMA_NORMAL;
        err = HAL_DMA_Init(&handle_GPDMA1_Channel3);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_Init", err);
        }

        __HAL_LINKDMA(spiHandle, hdmatx, handle_GPDMA1_Channel3);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel3, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_ConfigChannelAttributes", err);
        }

        // GPDMA1_REQUEST_SPI2_RX Init
        handle_GPDMA1_Channel2.Instance = GPDMA1_Channel2;
        handle_GPDMA1_Channel2.Init.Request = GPDMA1_REQUEST_SPI2_RX;
        handle_GPDMA1_Channel2.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        handle_GPDMA1_Channel2.Init.Direction = DMA_PERIPH_TO_MEMORY;
        handle_GPDMA1_Channel2.Init.SrcInc = DMA_SINC_FIXED;
        handle_GPDMA1_Channel2.Init.DestInc = DMA_DINC_INCREMENTED;
        handle_GPDMA1_Channel2.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel2.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel2.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel2.Init.SrcBurstLength = 1;
        handle_GPDMA1_Channel2.Init.DestBurstLength = 1;
        handle_GPDMA1_Channel2.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
        handle_GPDMA1_Channel2.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel2.Init.Mode = DMA_NORMAL;
        err = HAL_DMA_Init(&handle_GPDMA1_Channel2);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_Init", err);
        }

        __HAL_LINKDMA(spiHandle, hdmarx, handle_GPDMA1_Channel2);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel2, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_ConfigChannelAttributes", err);
        }

        // GPDMA1 interrupt Init
        HAL_NVIC_SetPriority(GPDMA1_Channel2_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel2_IRQn);
        HAL_NVIC_SetPriority(GPDMA1_Channel3_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel3_IRQn);

        // SPI2 interrupt Init
        HAL_NVIC_SetPriority(SPI2_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(SPI2_IRQn);
    } else if (spiHandle->Instance == SPI3) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_SPI3;
        PeriphClkInit.Spi3ClockSelection = RCC_SPI3CLKSOURCE_SYSCLK;
        err = HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_3, "HAL_RCCEx_PeriphCLKConfig", err);
        }

        // SPI3 clock enable
        __HAL_RCC_SPI3_CLK_ENABLE();

        __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE();
        // SPI3 GPIO Configuration
        // PD6     ------> SPI3_MOSI
        // PG9     ------> SPI3_SCK
        // PG10    ------> SPI3_MISO

        GPIO_InitStruct.Pin = SENSORBODY_MOSI_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF5_SPI3;
        HAL_GPIO_Init(SENSORBODY_MOSI_GPIO_Port, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = SENSORBODY_SCK_Pin | SENSORBODY_MISO_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF6_SPI3;
        HAL_GPIO_Init(GPIOG, &GPIO_InitStruct);

        // SPI3 DMA Init
        __HAL_RCC_GPDMA1_CLK_ENABLE();

        // GPDMA1_REQUEST_SPI3_TX Init
        handle_GPDMA1_Channel5.Instance = GPDMA1_Channel5;
        handle_GPDMA1_Channel5.Init.Request = GPDMA1_REQUEST_SPI3_TX;
        handle_GPDMA1_Channel5.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        handle_GPDMA1_Channel5.Init.Direction = DMA_MEMORY_TO_PERIPH;
        handle_GPDMA1_Channel5.Init.SrcInc = DMA_SINC_INCREMENTED;
        handle_GPDMA1_Channel5.Init.DestInc = DMA_DINC_FIXED;
        handle_GPDMA1_Channel5.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel5.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel5.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel5.Init.SrcBurstLength = 1;
        handle_GPDMA1_Channel5.Init.DestBurstLength = 1;
        handle_GPDMA1_Channel5.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
        handle_GPDMA1_Channel5.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel5.Init.Mode = DMA_NORMAL;
        err = HAL_DMA_Init(&handle_GPDMA1_Channel5);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_Init", err);
        }

        __HAL_LINKDMA(spiHandle, hdmatx, handle_GPDMA1_Channel5);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel5, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_ConfigChannelAttributes", err);
        }

        // GPDMA1_REQUEST_SPI3_RX Init
        handle_GPDMA1_Channel4.Instance = GPDMA1_Channel4;
        handle_GPDMA1_Channel4.Init.Request = GPDMA1_REQUEST_SPI3_RX;
        handle_GPDMA1_Channel4.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        handle_GPDMA1_Channel4.Init.Direction = DMA_PERIPH_TO_MEMORY;
        handle_GPDMA1_Channel4.Init.SrcInc = DMA_SINC_FIXED;
        handle_GPDMA1_Channel4.Init.DestInc = DMA_DINC_INCREMENTED;
        handle_GPDMA1_Channel4.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel4.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel4.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel4.Init.SrcBurstLength = 1;
        handle_GPDMA1_Channel4.Init.DestBurstLength = 1;
        handle_GPDMA1_Channel4.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
        handle_GPDMA1_Channel4.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel4.Init.Mode = DMA_NORMAL;
        err = HAL_DMA_Init(&handle_GPDMA1_Channel4);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_Init", err);
        }

        __HAL_LINKDMA(spiHandle, hdmarx, handle_GPDMA1_Channel4);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel4, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_ConfigChannelAttributes", err);
        }

        // GPDMA1 interrupt Init
        HAL_NVIC_SetPriority(GPDMA1_Channel4_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel4_IRQn);
        HAL_NVIC_SetPriority(GPDMA1_Channel5_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel5_IRQn);

        // SPI3 interrupt Init
        HAL_NVIC_SetPriority(SPI3_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(SPI3_IRQn);
    }
}

void Spi::MspDeinitCallback(SPI_HandleTypeDef *spiHandle)
{
    if (spiHandle->Instance == SPI1) {
        // Peripheral clock disable
        __HAL_RCC_SPI1_CLK_DISABLE();

        // SPI1 GPIO Configuration
        // PG2     ------> SPI1_SCK
        // PG3     ------> SPI1_MISO
        // PB5     ------> SPI1_MOSI
        HAL_GPIO_DeInit(GPIOG, SENSOR1_SCK_Pin | SENSOR1_MISO_Pin);
        HAL_GPIO_DeInit(SENSOR1_MOSI_GPIO_Port, SENSOR1_MOSI_Pin);

        // SPI1 DMA DeInit
        HAL_DMA_DeInit(spiHandle->hdmatx);
        HAL_DMA_DeInit(spiHandle->hdmarx);

        // SPI1 interrupt Deinit
        HAL_NVIC_DisableIRQ(SPI1_IRQn);
    } else if (spiHandle->Instance == SPI2) {
        // Peripheral clock disable
        __HAL_RCC_SPI2_CLK_DISABLE();

        // SPI2 GPIO Configuration
        // PC2     ------> SPI2_MISO
        // PC3     ------> SPI2_MOSI
        // PD1     ------> SPI2_SCK
        HAL_GPIO_DeInit(GPIOC, SENSOR2_MISO_Pin | SENSOR2_MOSI_Pin);
        HAL_GPIO_DeInit(SENSOR2_SCK_GPIO_Port, SENSOR2_SCK_Pin);

        // SPI2 DMA DeInit
        HAL_DMA_DeInit(spiHandle->hdmatx);
        HAL_DMA_DeInit(spiHandle->hdmarx);

        // SPI2 interrupt Deinit
        HAL_NVIC_DisableIRQ(SPI2_IRQn);
    } else if (spiHandle->Instance == SPI3) {
        // Peripheral clock disable
        __HAL_RCC_SPI3_CLK_DISABLE();

        // SPI3 GPIO Configuration
        // PD6     ------> SPI3_MOSI
        // PG9     ------> SPI3_SCK
        // PG10    ------> SPI3_MISO
        HAL_GPIO_DeInit(SENSORBODY_MOSI_GPIO_Port, SENSORBODY_MOSI_Pin);
        HAL_GPIO_DeInit(GPIOG, SENSORBODY_SCK_Pin | SENSORBODY_MISO_Pin);

        // SPI3 DMA DeInit
        HAL_DMA_DeInit(spiHandle->hdmatx);
        HAL_DMA_DeInit(spiHandle->hdmarx);

        // SPI3 interrupt Deinit
        HAL_NVIC_DisableIRQ(SPI3_IRQn);
    }
}

void Spi::TxRxCpltCallback(SPI_HandleTypeDef *spiHandle)
{
    Spi &instance = FindInstance(spiHandle);
    if (instance.mSpiCpltHandler != nullptr) {
        instance.mSpiCpltHandler->transferCplt(&instance, true);
    }
}

void Spi::ErrorCallback(SPI_HandleTypeDef *spiHandle)
{
    Spi &instance = FindInstance(spiHandle);
    if (instance.mSpiCpltHandler != nullptr) {
        instance.mSpiCpltHandler->transferCplt(&instance, false);
    }
}

} // namespace Driver
