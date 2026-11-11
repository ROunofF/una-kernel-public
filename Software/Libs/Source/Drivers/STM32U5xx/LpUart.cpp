/**
 ******************************************************************************
 * @file    LpUart.cpp
 * @brief   LPUART driver with STOP2 wake-up support.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "LpUart.hpp"

#define LOG_MODULE_PRX      "LpUart"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

#include <cassert>

#include "Sleep.hpp"

static DMA_HandleTypeDef handle_GPDMA1_Channel10;
static DMA_HandleTypeDef handle_GPDMA1_Channel11;
static DMA_NodeTypeDef   Node_GPDMA1_Channel11;
static DMA_QListTypeDef  List_GPDMA1_Channel11;

#if 0
#define LPUART_TP1_SET()    GPIOC->BSRR = GPIO_PIN_2;
#define LPUART_TP1_RESET()  GPIOC->BRR  = GPIO_PIN_2;
#define LPUART_TP2_SET()    GPIOC->BSRR = GPIO_PIN_3;
#define LPUART_TP2_RESET()  GPIOC->BRR  = GPIO_PIN_3;
#define LPUART_TP3_SET()    GPIOD->BSRR = GPIO_PIN_4;
#define LPUART_TP3_RESET()  GPIOD->BRR  = GPIO_PIN_4;
#define LPUART_TP4_SET()    GPIOD->BSRR = GPIO_PIN_5;
#define LPUART_TP4_RESET()  GPIOD->BRR  = GPIO_PIN_5;
#define LPUART_TP5_SET()    GPIOD->BSRR = GPIO_PIN_7;
#define LPUART_TP5_RESET()  GPIOD->BRR  = GPIO_PIN_7;
#else
#define LPUART_TP1_SET()
#define LPUART_TP1_RESET()
#define LPUART_TP2_SET()
#define LPUART_TP2_RESET()
#define LPUART_TP3_SET()
#define LPUART_TP3_RESET()
#define LPUART_TP4_SET()
#define LPUART_TP4_RESET()
#define LPUART_TP5_SET()
#define LPUART_TP5_RESET()
#endif

namespace Driver
{

#define LOG_HAL_ERR_MSG(port, msg, err) \
        LOG_ERROR("LPUART_%u: %s failed. Err %d\n", static_cast<uint8_t>(port) + 1, msg, err);

#define LOG_UART_ERR_MSG(port, msg, err) \
    do {    \
        uint32_t uartErr = HAL_UART_GetError(&GetInstance(port).mUartHandle); \
        LOG_ERROR("LPUART_%u: %s failed. HAL Err %d, UART err 0x%08X\n", static_cast<uint8_t>(port) + 1, msg, err, uartErr); \
    } while (0)

LpUart& LpUart::GetInstance(Port port)
{
    assert(port < LpUart::Port::PORT_NUM);

    uint8_t n = static_cast<uint8_t>(port);
    static LpUart sInstance[static_cast<uint8_t>(Port::PORT_NUM)];
    // Set the port once. Avoids a repeated write to shared state that is
    // also touched from ISR context (IRQ handlers, error log macro).
    if (sInstance[n].mPort != port) {
        sInstance[n].mPort = port;
    }
    return sInstance[n];
}

bool LpUart::init()
{
    if (mIsInitialized) {
        return true;
    }

    switch (mPort) {
        case Port::PORT_1:
            mUartIrq = LPUART1_IRQn;
            mTimIrq = TIM6_IRQn;
            mIsInitialized = LPUARTx_Init(LPUART1, TIM6);
            break;

        default:
            assert(false);
            break;
    }

    return mIsInitialized;
}

void LpUart::deinit()
{
    if (!mIsInitialized) {
        return;
    }

    LPUARTx_DeInit();

    mIsInitialized = false;
}

bool LpUart::recover()
{
    // Nothing to recover on a port that is intentionally down.
    if (!mIsInitialized) {
        return false;
    }

    // TX-targeted recovery: abort a wedged DMA transmit and reset the TX
    // state. abortTx() also resumes the sleep lock taken in transmit(), and
    // leaves the low-power RX reception untouched.
    abortTx();
    return true;
}

bool LpUart::startReception()
{
    if (!mIsInitialized) {
        return false;
    }

    HAL_NVIC_DisableIRQ(mUartIrq);
    if (mRxState == RxState::OFF) {

        UART_WakeUpTypeDef wakeup {};
        wakeup.WakeUpEvent = UART_WAKEUP_ON_READDATA_NONEMPTY;
        HAL_UARTEx_StopModeWakeUpSourceConfig(&mUartHandle, wakeup);

        __HAL_RCC_LPUART1_CLKAM_ENABLE();
        HAL_UARTEx_EnableStopMode(&mUartHandle);

        mLastReadPos = 0;
        StartReception(&mUartHandle, mpRxBuffer, mRxBufferSize);

        mDmaPosition = __HAL_DMA_GET_COUNTER(mUartHandle.hdmarx);
        mRxState = RxState::SLEEP;

        HAL_NVIC_EnableIRQ(mTimIrq);
    }
    HAL_NVIC_EnableIRQ(mUartIrq);

    return true;
}

bool LpUart::stopReception()
{
    if (!mIsInitialized) {
        return false;
    }

    HAL_NVIC_DisableIRQ(mTimIrq);
    HAL_NVIC_DisableIRQ(mUartIrq);
    if (mRxState != RxState::OFF) {

        StopIdleTimer(&mTimHandle);
        StopReception(&mUartHandle);

        mLastReadPos = 0;

        HAL_UARTEx_DisableStopMode(&mUartHandle);
        __HAL_RCC_LPUART1_CLKAM_DISABLE();

        if (mRxState != RxState::SLEEP) {
            Driver::Sleep::Resume();
        }

        mRxState = RxState::OFF;
    }
    HAL_NVIC_EnableIRQ(mUartIrq);

    return true;
}

bool LpUart::isReceptionActive()
{
    return mRxState != RxState::OFF;
}

bool LpUart::transmit(const uint8_t *pData, size_t size, uint32_t timeout)
{
    assert(pData != nullptr);

    if (!mIsInitialized) {
        return false;
    }

    HAL_StatusTypeDef err = HAL_ERROR;

    if (mTxState == TxState::TX) {
        return false;
    }

    // Blocking
    if (timeout > 0) {
        if (timeout == HAL_MAX_DELAY) {
            timeout--;
        }
        mTxState = TxState::TX;
        Driver::Sleep::Pause();

        err = HAL_UART_Transmit(&mUartHandle, const_cast<uint8_t*>(pData), size, timeout);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(mPort, "HAL_UART_Transmit", err);
        }

        Driver::Sleep::Resume();
        mTxState = TxState::OFF;

        return err == HAL_OK;
    }

    // DMA
    mTxState = TxState::TX;
    Driver::Sleep::Pause();

    err = HAL_UART_Transmit_DMA(&mUartHandle, const_cast<uint8_t*>(pData), size);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UART_Transmit_DMA", err);
        // Fallback
        Driver::Sleep::Resume();
        mTxState = TxState::OFF;
    }

    return err == HAL_OK;
}

bool LpUart::isTxBusy()
{
    return mTxState == TxState::TX;
}

bool LpUart::receive(uint8_t *pData, size_t size, size_t &br, uint32_t timeout)
{
    assert(pData != nullptr);

    if (!mIsInitialized) {
        return false;
    }

    if (size == 0) {
        return false;
    }

    HAL_StatusTypeDef err = HAL_ERROR;

    if (timeout > 0) {
        if (timeout == HAL_MAX_DELAY) {
            timeout--;
        }

        bool rxWasON = mRxState != RxState::OFF;

        Driver::Sleep::Pause();
        stopReception();

        err = HAL_UART_Receive(&mUartHandle, pData, size, timeout);
        if (err != HAL_OK) {
            if (err != HAL_TIMEOUT) {
                LOG_HAL_ERR_MSG(mPort, "HAL_UART_Receive", err);
            }
        } else {
            br = size;
        }

        if (rxWasON) {
            startReception();
        }
        Driver::Sleep::Resume();

        return err == HAL_OK;
    }

    br = receiveFromDMA(pData, size);

    return br > 0;
}

size_t LpUart::getAvailableDataSize()
{
    if (!mIsInitialized || mRxState == RxState::OFF) {
        return 0;
    }

    uint16_t currentCnt = __HAL_DMA_GET_COUNTER(mUartHandle.hdmarx);
    uint16_t currentPos = mRxBufferSize - currentCnt;

    if (mLastReadPos <= currentPos) {
        return currentPos - mLastReadPos;
    }

    return (mRxBufferSize - mLastReadPos + currentPos);
}

void LpUart::abortRx()
{
    stopReception();
}

void LpUart::abortTx()
{
    if (!mIsInitialized) {
        return;
    }

    HAL_NVIC_DisableIRQ(mUartIrq);
    if (mTxState != TxState::OFF) {
        HAL_UART_AbortTransmit(&mUartHandle);
        mTxState = TxState::OFF;
        Driver::Sleep::Resume();
    }
    HAL_NVIC_EnableIRQ(mUartIrq);
}

void LpUart::flushRx()
{
    if (!mIsInitialized) {
        return;
    }

    bool rxWasON = mRxState != RxState::OFF;
    stopReception();
    mLastReadPos = 0;
    if (rxWasON) {
        startReception();
    }
}

void LpUart::setBuffer(void *pBuff, size_t size)
{
    assert((reinterpret_cast<uint32_t>(pBuff) & 0x1F) == 0);
    assert((size & 0x1F) == 0);

    bool rxWasON = mRxState != RxState::OFF;
    stopReception();

    if (pBuff == nullptr || size == 0) {
        mpRxBuffer = mRxBuff;
        mRxBufferSize = sizeof(mRxBuff);
    } else {
        mpRxBuffer = reinterpret_cast<uint8_t*>(pBuff);
        mRxBufferSize = size;
    }

    if (rxWasON) {
        startReception();
    }
}

bool LpUart::setBaudrate(uint32_t baudrate)
{
    if (!mIsInitialized) {
        mBaudrate = baudrate;
        return true;
    }

    bool rxWasON = mRxState != RxState::OFF;
    stopReception();

    abortTx();

    HAL_StatusTypeDef err = SetBaudRateHAL(&mUartHandle, baudrate);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "SetBaudRateHAL", err);
    } else {
        mBaudrate = baudrate;
    }

    if (rxWasON) {
        startReception();
    }

    return err == HAL_OK;
}


LpUart& LpUart::FindInstance(UART_HandleTypeDef *uartHandle)
{
    if (uartHandle->Instance == LPUART1) {
        return GetInstance(Port::PORT_1);
    }

    assert(false);
    return GetInstance(Port::PORT_1);
}

LpUart& LpUart::FindInstance(TIM_HandleTypeDef *timHandle)
{
    if (timHandle->Instance == TIM6) {
        return GetInstance(Port::PORT_1);
    }

    assert(false);
    return GetInstance(Port::PORT_1);
}

void LpUart::StartReception(UART_HandleTypeDef *uartHandle, uint8_t *buff, uint16_t size)
{
    // Use HAL function that handles IDLE detection automatically
    // This sets ReceptionType to HAL_UART_RECEPTION_TOIDLE and enables IDLE interrupt
    HAL_UARTEx_ReceiveToIdle_DMA(uartHandle, buff, size);

    // Enable RXNE interrupt ONLY for wake-up
    // This will wake CPU from STOP2 on first byte
    // NOTE! This will NOT set UART_FLAG_RXNE status Flag
    __HAL_UART_ENABLE_IT(uartHandle, UART_IT_RXNE);
}

void LpUart::StopReception(UART_HandleTypeDef *uartHandle)
{
    __HAL_UART_DISABLE_IT(uartHandle, UART_IT_RXNE);

    HAL_UART_AbortReceive(uartHandle);
}

uint16_t LpUart::receiveFromDMA(uint8_t *pData, uint16_t maxSize)
{
    uint16_t currentCnt = __HAL_DMA_GET_COUNTER(mUartHandle.hdmarx);
    uint16_t currentPos = mRxBufferSize - currentCnt;

    uint16_t available = 0;

    if (mLastReadPos <= currentPos) {
        available = currentPos - mLastReadPos;
    } else {
        available = mRxBufferSize - mLastReadPos + currentPos;
    }

    if (available == 0) {
        return 0;
    }

    uint16_t toRead = (available < maxSize) ? available : maxSize;
    uint16_t read = 0;

    while (read < toRead) {
        pData[read++] = mpRxBuffer[mLastReadPos++];

        if (mLastReadPos >= mRxBufferSize) {
            mLastReadPos = 0;
        }
    }

    return read;
}


/**
 * @brief   Get actual MSIK frequency from RCC configuration.
 * @retval  MSIK frequency in Hz.
 */
static uint32_t GetMSIKFrequency(void)
{
    uint32_t msirange = (RCC->ICSCR1 & RCC_ICSCR1_MSIKRANGE) >> RCC_ICSCR1_MSIKRANGE_Pos;
    const uint32_t MSIRangeTable[16] = {
        48000000, 24000000, 16000000, 12000000,
        4000000,  2000000,  1330000,  1000000,
        3072000,  1536000,  1024000,  768000,
        400000,   200000,   133000,   100000
    };
    return (msirange < 16) ? MSIRangeTable[msirange] : 4000000;
}

static uint32_t GetLPUART1ClockSource(void)
{
    return (RCC->CCIPR3 & RCC_CCIPR3_LPUART1SEL) >> RCC_CCIPR3_LPUART1SEL_Pos;
}

static uint32_t GetLPUARTClockFreq(void)
{
    uint32_t sel = GetLPUART1ClockSource();
    switch (sel) {
        case 0: return HAL_RCC_GetPCLK3Freq();
        case 1: return HAL_RCC_GetSysClockFreq();
        case 2: return HSI_VALUE;
        case 3: return LSE_VALUE;
        case 4: return GetMSIKFrequency();
        default: return HAL_RCC_GetPCLK3Freq();
    }
}

HAL_StatusTypeDef LpUart::SetBaudRateHAL(UART_HandleTypeDef *uartHandle,
        uint32_t newBaudRate)
{
    if (uartHandle->gState != HAL_UART_STATE_READY) {
        return HAL_BUSY;
    }

    // Get actual clock frequency
    uint32_t lpuart_clk = GetLPUARTClockFreq();

    // Calculate BRR value using LPUART formula
    // BRR = (256 * lpuart_clk) / baudrate
    uint32_t brr = UART_DIV_LPUART(lpuart_clk, newBaudRate,
                                    uartHandle->Init.ClockPrescaler);

    // Validate BRR value (must be >= 0x300 for LPUART)
    if (brr < 0x300) {
        LOG_ERROR("BRR value 0x%03X too low for baudrate %u with clock %u Hz\n",
                  brr, newBaudRate, lpuart_clk);
        return HAL_ERROR;
    }

    // Set baudrate register
    uartHandle->Instance->BRR = brr;

    // Update handle
    uartHandle->Init.BaudRate = newBaudRate;

    LOG_DEBUG("Baudrate changed: %u baud, clock=%u Hz, BRR=0x%04X\n",
              newBaudRate, lpuart_clk, brr);

    return HAL_OK;
}

bool LpUart::LPUARTx_Init(USART_TypeDef *pInstance, TIM_TypeDef* pTimInstance)
{
    mUartHandle.Instance = pInstance;
    mUartHandle.Init.BaudRate = mBaudrate;
    mUartHandle.Init.WordLength = UART_WORDLENGTH_8B;
    mUartHandle.Init.StopBits = UART_STOPBITS_1;
    mUartHandle.Init.Parity = UART_PARITY_NONE;
    mUartHandle.Init.Mode = UART_MODE_TX_RX;
    mUartHandle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    mUartHandle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    mUartHandle.Init.ClockPrescaler = UART_PRESCALER_DIV1;

    mUartHandle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    mUartHandle.MspInitCallback = MspInitCallback;
    mUartHandle.MspDeInitCallback = MspDeinitCallback;

    HAL_StatusTypeDef err = HAL_UART_Init(&mUartHandle);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UART_Init", err);
        return false;
    }

    err = HAL_UARTEx_SetTxFifoThreshold(&mUartHandle, UART_TXFIFO_THRESHOLD_1_8);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UARTEx_SetTxFifoThreshold", err);
    }

    err = HAL_UARTEx_SetRxFifoThreshold(&mUartHandle, UART_RXFIFO_THRESHOLD_1_8);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UARTEx_SetRxFifoThreshold", err);
    }

    err = HAL_UARTEx_EnableFifoMode(&mUartHandle);
    //err = HAL_UARTEx_DisableFifoMode(&mUartHandle);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UARTEx_EnableFifoMode", err);
    }

    mUartHandle.TxCpltCallback = TxCpltCallback;
    mUartHandle.ErrorCallback = ErrorCallback;
    mUartHandle.RxCpltCallback = RxCpltCallback;
    mUartHandle.RxEventCallback = RxEventCallback;

    // Initialize timer to determine the end of reception
    TIMx_Init(pTimInstance);

    return true;
}

void LpUart::LPUARTx_DeInit()
{
    // Abort RX
    stopReception();

    // Abort TX
    abortTx();

    HAL_UART_DeInit(&mUartHandle);
    TIMx_DeInit();
}

void LpUart::MspInitCallback(UART_HandleTypeDef *uartHandle)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
    HAL_StatusTypeDef err = HAL_ERROR;

    if (uartHandle->Instance == LPUART1) {

        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_LPUART1;
        PeriphClkInit.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_MSIK;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
            Error_Handler();
        }

        __HAL_RCC_LPUART1_CLK_ENABLE();

        __HAL_RCC_GPIOA_CLK_ENABLE();

        // LPUART1 GPIO Configuration
        // PA2     ------> LPUART1_TX
        // PA3     ------> LPUART1_RX
        GPIO_InitStruct.Pin = SENSOR2_TX_INT0_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF8_LPUART1;
        HAL_GPIO_Init(SENSOR2_TX_INT0_GPIO_Port, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = SENSOR2_RX_INT1_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF8_LPUART1;
        HAL_GPIO_Init(SENSOR2_RX_INT1_GPIO_Port, &GPIO_InitStruct);

        __HAL_RCC_GPDMA1_CLK_ENABLE();

        handle_GPDMA1_Channel10.Instance = GPDMA1_Channel10;
        handle_GPDMA1_Channel10.Init.Request = GPDMA1_REQUEST_LPUART1_TX;
        handle_GPDMA1_Channel10.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        handle_GPDMA1_Channel10.Init.Direction = DMA_MEMORY_TO_PERIPH;
        handle_GPDMA1_Channel10.Init.SrcInc = DMA_SINC_INCREMENTED;
        handle_GPDMA1_Channel10.Init.DestInc = DMA_DINC_FIXED;
        handle_GPDMA1_Channel10.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel10.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel10.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel10.Init.SrcBurstLength = 1;
        handle_GPDMA1_Channel10.Init.DestBurstLength = 1;
        handle_GPDMA1_Channel10.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
        handle_GPDMA1_Channel10.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel10.Init.Mode = DMA_NORMAL;

        err = HAL_DMA_Init(&handle_GPDMA1_Channel10);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_Init", err);
        }

        __HAL_LINKDMA(uartHandle, hdmatx, handle_GPDMA1_Channel10);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel10, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_ConfigChannelAttributes", err);
        }

        HAL_NVIC_SetPriority(GPDMA1_Channel10_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel10_IRQn);

        DMA_NodeConfTypeDef NodeConfig = {0};
        NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
        NodeConfig.Init.Request = GPDMA1_REQUEST_LPUART1_RX;
        NodeConfig.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        NodeConfig.Init.Direction = DMA_PERIPH_TO_MEMORY;
        NodeConfig.Init.SrcInc = DMA_SINC_FIXED;
        NodeConfig.Init.DestInc = DMA_DINC_INCREMENTED;
        NodeConfig.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        NodeConfig.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        NodeConfig.Init.SrcBurstLength = 1;
        NodeConfig.Init.DestBurstLength = 1;
        NodeConfig.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT0;
        NodeConfig.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        NodeConfig.Init.Mode = DMA_NORMAL;
        NodeConfig.TriggerConfig.TriggerPolarity = DMA_TRIG_POLARITY_MASKED;
        NodeConfig.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
        NodeConfig.DataHandlingConfig.DataAlignment = DMA_DATA_RIGHTALIGN_ZEROPADDED;
        err = HAL_DMAEx_List_BuildNode(&NodeConfig, &Node_GPDMA1_Channel11);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_BuildNode", err);
        }

        err = HAL_DMAEx_List_InsertNode(&List_GPDMA1_Channel11, NULL, &Node_GPDMA1_Channel11);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_InsertNode", err);
        }

        err = HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel11);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_SetCircularMode", err);
        }

        handle_GPDMA1_Channel11.Instance = GPDMA1_Channel11;
        handle_GPDMA1_Channel11.InitLinkedList.Priority = DMA_HIGH_PRIORITY;
        handle_GPDMA1_Channel11.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
        handle_GPDMA1_Channel11.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
        handle_GPDMA1_Channel11.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel11.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
        err = HAL_DMAEx_List_Init(&handle_GPDMA1_Channel11);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_Init", err);
        }

        err = HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel11, &List_GPDMA1_Channel11);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_LinkQ", err);
        }

        __HAL_LINKDMA(uartHandle, hdmarx, handle_GPDMA1_Channel11);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel11, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_ConfigChannelAttributes", err);
        }

        HAL_NVIC_SetPriority(GPDMA1_Channel11_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel11_IRQn);

        HAL_NVIC_SetPriority(LPUART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(LPUART1_IRQn);
    }
}

void LpUart::MspDeinitCallback(UART_HandleTypeDef *uartHandle)
{
    if (uartHandle->Instance == LPUART1) {
        __HAL_RCC_LPUART1_CLK_DISABLE();

        HAL_GPIO_DeInit(SENSOR2_TX_INT0_GPIO_Port, SENSOR2_TX_INT0_Pin);
        HAL_GPIO_DeInit(SENSOR2_RX_INT1_GPIO_Port, SENSOR2_RX_INT1_Pin);

        HAL_DMA_DeInit(&handle_GPDMA1_Channel10);
        HAL_DMA_DeInit(&handle_GPDMA1_Channel11);

        HAL_NVIC_DisableIRQ(LPUART1_IRQn);
    }
}

void LpUart::TxCpltCallback(UART_HandleTypeDef *uartHandle)
{
    LpUart &instance = FindInstance(uartHandle);
    if (instance.mTxState != TxState::OFF) {
        if (instance.mUartCpltHandler != nullptr) {
            instance.mUartCpltHandler->transmitCplt(&instance, true);
        }
        instance.mTxState = TxState::OFF;
        Driver::Sleep::Resume();
    }
}


void LpUart::ErrorCallback(UART_HandleTypeDef *uartHandle)
{
    LpUart &instance = FindInstance(uartHandle);

#if 0 // Many errors when switching baud rate
    LOG_UART_ERR_MSG(instance.mPort, "", 0);
#endif

    if (instance.mRxState != RxState::OFF) {
        StopIdleTimer(&instance.mTimHandle);

        if (instance.mRxState != RxState::SLEEP) {
            Driver::Sleep::Resume();
        }

        // Stop DMA completely
        HAL_UART_DMAStop(&instance.mUartHandle);

        // Reset read position
        instance.mLastReadPos = 0;

        // Restart reception
        instance.mDmaPosition = __HAL_DMA_GET_COUNTER(instance.mUartHandle.hdmarx);
        instance.mRxState = RxState::SLEEP;

        StartReception(&instance.mUartHandle, instance.mpRxBuffer, instance.mRxBufferSize);
    }


    if (instance.mTxState != TxState::OFF) {
        if (instance.mUartCpltHandler != nullptr) {
            instance.mUartCpltHandler->transmitCplt(&instance, false);
        }
        instance.mTxState = TxState::OFF;
        Driver::Sleep::Resume();
    }

}

void LpUart::RxCpltCallback(UART_HandleTypeDef *uartHandle)
{
}

void LpUart::RxEventCallback(UART_HandleTypeDef *uartHandle, uint16_t Pos)
{
    LPUART_TP3_SET();

    LpUart &instance = FindInstance(uartHandle);
    if (instance.mRxState == RxState::RX) {
        // Notify a listener about the available received data
        size_t available = instance.getAvailableDataSize();
        if (available > 0) {
            if (instance.mUartCpltHandler != nullptr) {
                instance.mUartCpltHandler->receiveCplt(&instance, available);
            }
        }

        // Waiting for next bytes
        __HAL_UART_ENABLE_IT(&instance.mUartHandle, UART_IT_RXNE);
        StartIdleTimer(&instance.mTimHandle);

        instance.mDmaPosition = __HAL_DMA_GET_COUNTER(instance.mUartHandle.hdmarx);
        instance.mRxState = RxState::TIMEOUT;
    }

    LPUART_TP3_RESET();
}

void LpUart::PeriodElapsedCallback(TIM_HandleTypeDef* timHandle)
{
    LpUart &instance = FindInstance(timHandle);
    // Stop timer
    StopIdleTimer(&instance.mTimHandle);

    if (instance.mRxState == RxState::TIMEOUT) {

        instance.mDmaPosition = __HAL_DMA_GET_COUNTER(instance.mUartHandle.hdmarx);
        instance.mRxState = RxState::SLEEP;

        Driver::Sleep::Resume();
    }
}

void LpUart::IrqHandlerPort1()
{
    LPUART_TP1_SET();

    LpUart &instance = GetInstance(Port::PORT_1);

    uint16_t dmaPos = __HAL_DMA_GET_COUNTER(instance.mUartHandle.hdmarx);

    if (instance.mDmaPosition != dmaPos) { // Received something

        if (instance.mRxState == RxState::SLEEP) {
            LPUART_TP2_SET();

            // Disable the UART_IT_RXNE interrupt, because it is no longer needed.
            // Waiting for the end of reception - UART_FLAG_IDLE (see RxEventCallback)
            // to receive data
            __HAL_UART_DISABLE_IT(&instance.mUartHandle, UART_IT_RXNE);

            Driver::Sleep::Pause();

            instance.mDmaPosition = dmaPos;
            instance.mRxState = RxState::RX;

            LPUART_TP2_RESET();
        } else if (instance.mRxState == RxState::TIMEOUT) {
            LPUART_TP2_SET();

            // Disable the UART_IT_RXNE interrupt, because it is no longer needed.
            // Waiting for the end of reception - UART_FLAG_IDLE (see RxEventCallback)
            // to receive data
            __HAL_UART_DISABLE_IT(&instance.mUartHandle, UART_IT_RXNE);

            StopIdleTimer(&instance.mTimHandle);

            instance.mDmaPosition = dmaPos;
            instance.mRxState = RxState::RX;

            LPUART_TP2_RESET();
        }
    }

    HAL_UART_IRQHandler(&instance.mUartHandle);

    LPUART_TP1_RESET();
}

void LpUart::IrqDmaCh10HandlerPort1()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel10);
}

void LpUart::IrqDmaCh11HandlerPort1()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel11);
}

void LpUart::IrqHandlerTimPort1()
{
    LPUART_TP5_SET();
    LpUart &instance = GetInstance(Port::PORT_1);

    // Process other events
    HAL_TIM_IRQHandler(&instance.mTimHandle);

    LPUART_TP5_RESET();
}

void LpUart::TIMx_Init(TIM_TypeDef* pInstance)
{
    if (pInstance == TIM6) {
        __HAL_RCC_TIM6_CLK_ENABLE();
        HAL_NVIC_SetPriority(TIM6_IRQn, 5, 0);
        //HAL_NVIC_EnableIRQ(TIM6_IRQn); Enable IRQ in startReception
    }

    mTimHandle.Instance = pInstance;
    mTimHandle.Init.Prescaler = 160;
    mTimHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    mTimHandle.Init.Period = 5000;

    mTimHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_StatusTypeDef err = HAL_TIM_Base_Init(&mTimHandle);

    mTimHandle.PeriodElapsedCallback = PeriodElapsedCallback;

    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_TIM_Base_Init", err);
    }
}

void LpUart::TIMx_DeInit()
{
    HAL_TIM_Base_DeInit(&mTimHandle);

    if (mTimHandle.Instance == TIM6) {
        HAL_NVIC_DisableIRQ(TIM6_IRQn);
        __HAL_RCC_TIM6_CLK_DISABLE();
    }
}

void LpUart::StartIdleTimer(TIM_HandleTypeDef* timHandle)
{
    StopIdleTimer(timHandle);

    LPUART_TP4_SET();

    __HAL_TIM_ENABLE_IT(timHandle, TIM_IT_UPDATE);
    __HAL_TIM_ENABLE(timHandle);
}

void LpUart::StopIdleTimer(TIM_HandleTypeDef* timHandle)
{
    __HAL_TIM_DISABLE_IT(timHandle, TIM_IT_UPDATE);
    __HAL_TIM_DISABLE(timHandle);

    __HAL_TIM_CLEAR_IT(timHandle, TIM_IT_UPDATE);
    __HAL_TIM_SET_COUNTER(timHandle, 0);

    LPUART_TP4_RESET();
}

bool LpUart::IsIdleTimerActive(TIM_HandleTypeDef* timHandle)
{
    return (timHandle->Instance->CR1 & TIM_CR1_CEN) == TIM_CR1_CEN;
}



} // namespace Driver
