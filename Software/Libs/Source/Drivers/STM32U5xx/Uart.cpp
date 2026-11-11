/**
 ******************************************************************************
 * @file    Uart.cpp
 * @brief   UART driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Uart.hpp"

#define LOG_MODULE_PRX      "Uart"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

#include <cassert>


// USART1 Tx
static DMA_HandleTypeDef handle_GPDMA1_Channel6;
// USART1 Rx
static DMA_HandleTypeDef handle_GPDMA1_Channel7;
static DMA_NodeTypeDef   Node_GPDMA1_Channel7;
static DMA_QListTypeDef  List_GPDMA1_Channel7;

// USART2 Tx
static DMA_HandleTypeDef handle_GPDMA1_Channel8;
// USART2 Rx
static DMA_HandleTypeDef handle_GPDMA1_Channel9;
static DMA_NodeTypeDef   Node_GPDMA1_Channel9;
static DMA_QListTypeDef  List_GPDMA1_Channel9;


namespace Driver
{

#define LOG_HAL_ERR_MSG(port, msg, err) \
        LOG_ERROR("PORT_%u: %s failed. Err %d\n", static_cast<uint8_t>(port) + 1, msg, err);

#define LOG_UART_ERR_MSG(port, msg, err) \
    do {    \
        uint32_t uartErr = HAL_UART_GetError(&GetInstance(port).mUartHandle); \
        LOG_ERROR("PORT_%u: %s failed. HAL Err %d, UART err 0x%08X\n", static_cast<uint8_t>(port) + 1, msg, err, uartErr); \
    } while (0)

Uart& Uart::GetInstance(Port port)
{
    assert(port < Uart::Port::PORT_NUM);

    uint8_t n = static_cast<uint8_t>(port);
    static Uart sInstance[static_cast<uint8_t>(Port::PORT_NUM)];
    // Set the port once. Avoids a repeated write to shared state that is
    // also touched from ISR context (IRQ handlers, error log macro).
    if (sInstance[n].mPort != port) {
        sInstance[n].mPort = port;
    }
    return sInstance[n];
}

bool Uart::init()
{
    switch (mPort) {
        case Port::PORT_1:
            return UARTx_Init(USART1);
        case Port::PORT_2:
            return UARTx_Init(USART2);

        default:
            assert(false);
            break;
    }

    return false;
}

void Uart::deinit()
{
    UARTx_DeInit();
}

bool Uart::recover()
{
    // Nothing to recover on a port that is intentionally down.
    if (!mIsInitialized) {
        return false;
    }

    // TX-targeted recovery: abort a wedged DMA transmit and clear the busy
    // flag, returning the HAL TX state to READY. The continuous RX reception
    // is deliberately left untouched - it has its own restart path in
    // ErrorCallback.
    HAL_StatusTypeDef err = HAL_UART_AbortTransmit(&mUartHandle);
    mTxBusy = false;
    return err == HAL_OK;
}

bool Uart::startReception()
{
    if (!mIsInitialized) {
        return false;
    }

    if (!mIsReceptionActive) {
        StartReception(&mUartHandle, mpRxBuffer, mRxBufferSize);
        mLastReadPos = 0;
        mIsReceptionActive = true;
    }

    return true;
}

bool Uart::stopReception()
{
    if (!mIsInitialized) {
        return false;
    }

    StopReception(&mUartHandle);
    mIsReceptionActive = false;

    return true;
}

bool Uart::isReceptionActive()
{
    return mIsReceptionActive;
}

bool Uart::transmit(const uint8_t *pData, size_t size, uint32_t timeout)
{
    assert(pData != nullptr);
    HAL_StatusTypeDef err = HAL_ERROR;

    if (mTxBusy) {
        return false;
    }

    mTxBusy = true;

    // Blocking
    if (timeout > 0) {
        if (timeout == HAL_MAX_DELAY) {
            timeout--;
        }
        err = HAL_UART_Transmit(&mUartHandle, const_cast<uint8_t*>(pData), size, timeout);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(mPort, "HAL_UART_Transmit", err);
            mTxBusy = false;
            return false;
        }
        mTxBusy = false;
        return true;
    }

    // DMA
    err = HAL_UART_Transmit_DMA(&mUartHandle, const_cast<uint8_t*>(pData), size);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UART_Transmit_DMA", err);
        mTxBusy = false;
        return false;
    }
    return true;
}

bool Uart::isTxBusy()
{
    return mTxBusy;
}

bool Uart::receive(uint8_t *pData, size_t size, size_t &br, uint32_t timeout)
{
    assert(pData != nullptr);

    if (size == 0) {
        return false;
    }

    HAL_StatusTypeDef err = HAL_ERROR;

    if (timeout > 0) {
        if (timeout == HAL_MAX_DELAY) {
            timeout--;
        }
        // Stop DMA if any
        if (HAL_IS_BIT_SET(mUartHandle.Instance->CR3, USART_CR3_DMAR)) {
            StopReception(&mUartHandle);
        }

        err = HAL_UART_Receive(&mUartHandle, pData, size, timeout);
        if (err != HAL_OK) {
            if (err != HAL_TIMEOUT) {
                LOG_HAL_ERR_MSG(mPort, "HAL_UART_Receive", err);
            }
        } else {
            br = size;
        }

        if (mIsReceptionActive) {
            StartReception(&mUartHandle, mpRxBuffer, mRxBufferSize);
        }
        return err == HAL_OK;
    }

    br = receiveFromDMA(pData, size);

    return br > 0;
}

size_t Uart::getAvailableDataSize()
{
    if (!mIsInitialized || !mIsReceptionActive) {
        return 0;
    }

    uint16_t currentCnt = __HAL_DMA_GET_COUNTER(mUartHandle.hdmarx);
    uint16_t currentPos = mRxBufferSize - currentCnt; // Real position in the buffer
    //LOG_DEBUG("currentCnt %u, mLastReadPos %u\n", currentCnt, mLastReadPos);

    if (mLastReadPos <= currentPos) {
        return currentPos - mLastReadPos;
    }

    // The buffer "turned around"
    return (mRxBufferSize - mLastReadPos + currentPos);
}

void Uart::abortRx()
{
    // TODO: need implementation
}

void Uart::flushRx()
{
    LOG_DEBUG("\n");
    if (HAL_IS_BIT_SET(mUartHandle.Instance->CR3, USART_CR3_DMAR)) {
        StopReception(&mUartHandle);
        mLastReadPos = 0;
        StartReception(&mUartHandle, mpRxBuffer, mRxBufferSize);
    } else {
        mLastReadPos = 0;
    }

}

void Uart::setBuffer(void *pBuff, size_t size)
{
    // The buffer must be 32-byte aligned
    // and its size must be a multiple of 32 bytes
    assert((reinterpret_cast<uint32_t>(pBuff) & 0x1F) == 0);
    assert((size & 0x1F) == 0);

    if (pBuff == nullptr || size == 0) {
        mpRxBuffer = mRxBuff;
        mRxBufferSize = sizeof(mRxBuff);
    } else {
        mpRxBuffer = reinterpret_cast<uint8_t*>(pBuff);
        mRxBufferSize = size;
    }
}

bool Uart::setBaudrate(uint32_t baudrate)
{
    if (!mIsInitialized) {
        mBaudrate = baudrate;
        return true;
    }

    HAL_StatusTypeDef err = HAL_ERROR;

    if (mIsReceptionActive) {
        if (HAL_IS_BIT_SET(mUartHandle.Instance->CR3, USART_CR3_DMAR)) {
            StopReception(&mUartHandle);
            mLastReadPos = 0;
        }
    }

    err = SetBaudRateHAL(&mUartHandle, baudrate);
    if (err != HAL_OK) {
        LOG_ERROR("SetBaudRateHAL. Err %d\n", err);
        return false;
    }
    LOG_INFO("Baudrate %u\n", baudrate);

    if (mIsReceptionActive) {
        StartReception(&mUartHandle, mpRxBuffer, mRxBufferSize);
    }
    mBaudrate = baudrate;

    return true;
}

void Uart::IrqHandlerPort1()
{
    Uart& uart = GetInstance(Port::PORT_1);

    // The HAL driver treats RTO as an error and stops cyclic DMA.
    // To prevent this from happening, we will intercept the event
    // here and clear it
    RtoHandler(uart);

    HAL_UART_IRQHandler(&uart.mUartHandle);
}

void Uart::IrqDmaCh6HandlerPort1()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel6);
}

void Uart::IrqDmaCh7HandlerPort1()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel7);
}

void Uart::IrqHandlerPort2()
{
    Uart& uart = GetInstance(Port::PORT_2);

    // The HAL driver treats RTO as an error and stops cyclic DMA.
    // To prevent this from happening, we will intercept the event
    // here and clear it
    RtoHandler(uart);

    HAL_UART_IRQHandler(&uart.mUartHandle);
}

void Uart::IrqDmaCh8HandlerPort2()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel8);
}

void Uart::IrqDmaCh9HandlerPort2()
{
    HAL_DMA_IRQHandler(&handle_GPDMA1_Channel9);
}

Uart& Uart::FindInstance(UART_HandleTypeDef *uartHandle)
{
    for (uint8_t i = 0; i < static_cast<uint8_t>(Port::PORT_NUM); i++) {
        Uart &instance = GetInstance(static_cast<Port>(i));
        if (&instance.mUartHandle == uartHandle) {
            return instance;
        }
    }
    assert(false);
    // Should never happen. Prevent compiler warning
    return GetInstance(Port::PORT_1);
}

void Uart::StartReception(UART_HandleTypeDef *uartHandle, uint8_t *buff, uint16_t size)
{
    HAL_StatusTypeDef err = HAL_ERROR;

    HAL_UART_ReceiverTimeout_Config(uartHandle, 100);

    err = HAL_UART_Receive_DMA(uartHandle, buff, size);

    if (err != HAL_OK) {
        LOG_ERROR("HAL_UART_Receive_DMA. Err %d\n", err);
    }

    HAL_UART_EnableReceiverTimeout(uartHandle);
    __HAL_UART_ENABLE_IT(uartHandle, UART_IT_RTO);
}

void Uart::StopReception(UART_HandleTypeDef *uartHandle)
{
    HAL_StatusTypeDef err = HAL_ERROR;

    __HAL_UART_DISABLE_IT(uartHandle, UART_IT_RTO);
    HAL_UART_DisableReceiverTimeout(uartHandle);

    if (HAL_IS_BIT_SET(uartHandle->Instance->CR3, USART_CR3_DMAR)) {
        err = HAL_UART_DMAStop(uartHandle);
        if (err != HAL_OK) {
            LOG_ERROR("HAL_UART_DMAStop. Err %d\n", err);
        }
    }
}

void Uart::RtoHandler(Uart& uart)
{
    uint32_t isrflags = READ_REG(uart.mUartHandle.Instance->ISR);
    uint32_t cr1its = READ_REG(uart.mUartHandle.Instance->CR1);

    if (((isrflags & USART_ISR_RTOF) != 0U) && ((cr1its & USART_CR1_RTOIE) != 0U)) {
        __HAL_UART_CLEAR_FLAG(&uart.mUartHandle, UART_CLEAR_RTOF);

        if (uart.mUartCpltHandler != nullptr) {
            uart.mUartCpltHandler->receiveCplt(&uart, uart.getAvailableDataSize());
        }
    }
}

uint16_t Uart::receiveFromDMA(uint8_t *buff, uint16_t maxSize)
{
    assert(buff != nullptr);

    if (maxSize == 0) {
        return 0;
    }

    uint16_t available = getAvailableDataSize();
    if (available == 0) {
        return 0;
    }

    uint16_t toCopy = (available > maxSize) ? maxSize : available;
    uint16_t copied = 0;

    // Copy taking into account the circularity of the buffer
    while (copied < toCopy) {
        buff[copied] = mpRxBuffer[mLastReadPos];
        mLastReadPos = (mLastReadPos + 1) % mRxBufferSize;
        copied++;
    }

    return copied;
}

HAL_StatusTypeDef Uart::SetBaudRateHAL(UART_HandleTypeDef *uartHandle,
        uint32_t newBaudRate)
{
    uint32_t clocksource;
    uint32_t pclk;
    uint32_t usartdiv;
    uint32_t prescaler;

    // Get prescaler from register
    prescaler = READ_REG(uartHandle->Instance->PRESC) & USART_PRESC_PRESCALER;

    // Get UART clock rate
    UART_GETCLOCKSOURCE(uartHandle, clocksource);
    pclk = HAL_RCCEx_GetPeriphCLKFreq(clocksource);

    // Calculate new BRR value
    if (IS_LPUART_INSTANCE(uartHandle->Instance)) {
        // For LPUART
        uint32_t minBRR = 0x00000300U;
        uint32_t maxBRR = 0x000FFFFFU;
        usartdiv = (uint32_t)(UART_DIV_LPUART(pclk, newBaudRate, prescaler));

        if (usartdiv < minBRR || usartdiv > maxBRR) {
            return HAL_ERROR;
        }
    } else {
        // For a regular UART
        uint32_t minBRR = 0x10U;
        uint32_t maxBRR = 0x0000FFFFU;

        if ((READ_REG(uartHandle->Instance->CR1) & USART_CR1_OVER8) == USART_CR1_OVER8) {
            usartdiv = (uint32_t)(UART_DIV_SAMPLING8(pclk, newBaudRate, prescaler));
        } else {
            usartdiv = (uint32_t)(UART_DIV_SAMPLING16(pclk, newBaudRate, prescaler));
        }

        if (usartdiv < minBRR || usartdiv > maxBRR) {
            return HAL_ERROR;
        }
    }

    // Critical section - disable UART, change BRR, enable
    __disable_irq();

    // Disable UART
    CLEAR_BIT(uartHandle->Instance->CR1, USART_CR1_UE);

    // Change BRR
    WRITE_REG(uartHandle->Instance->BRR, usartdiv);

    // Enable UART
    SET_BIT(uartHandle->Instance->CR1, USART_CR1_UE);

    __enable_irq();

    // Update HAL structure
    uartHandle->Init.BaudRate = newBaudRate;

    return HAL_OK;
}

bool Uart::UARTx_Init(USART_TypeDef *pInstance)
{
    if (mIsInitialized) {
        return true;
    }

    HAL_StatusTypeDef err = HAL_ERROR;

    mUartHandle.Instance = pInstance;
    mUartHandle.Init.BaudRate = mBaudrate;
    mUartHandle.Init.WordLength = UART_WORDLENGTH_8B;
    mUartHandle.Init.StopBits = UART_STOPBITS_1;
    mUartHandle.Init.Parity = UART_PARITY_NONE;
    mUartHandle.Init.Mode = UART_MODE_TX_RX;
    mUartHandle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    mUartHandle.Init.OverSampling = UART_OVERSAMPLING_16;
    mUartHandle.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    mUartHandle.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    mUartHandle.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;

    // Set MSP Callbacks
    mUartHandle.MspInitCallback = MspInitCallback;
    mUartHandle.MspDeInitCallback = MspDeinitCallback;


    err = HAL_UART_Init(&mUartHandle);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UART_Init", err);
        return false;
    }

    err = HAL_UARTEx_SetTxFifoThreshold(&mUartHandle, UART_TXFIFO_THRESHOLD_1_8);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UARTEx_SetTxFifoThreshold", err);
        return false;
    }

    err = HAL_UARTEx_SetRxFifoThreshold(&mUartHandle, UART_RXFIFO_THRESHOLD_1_8);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UARTEx_SetRxFifoThreshold", err);
        return false;
    }

    err = HAL_UARTEx_DisableFifoMode(&mUartHandle);
    if (err != HAL_OK) {
        LOG_HAL_ERR_MSG(mPort, "HAL_UARTEx_DisableFifoMode", err);
        return false;
    }

    // Set Other Callbacks
    mUartHandle.TxCpltCallback = TxCpltCallback;
    mUartHandle.RxCpltCallback = RxCpltCallback;
    mUartHandle.ErrorCallback = ErrorCallback;

    mTxBusy = false;
    mLastReadPos = 0;
    mIsReceptionActive = false;

    mIsInitialized = true;
    return true;
}

void Uart::UARTx_DeInit()
{
    if (!mIsInitialized) {
        return;
    }

    HAL_UART_DeInit(&mUartHandle);
    mIsInitialized = false;
}

void Uart::MspInitCallback(UART_HandleTypeDef *uartHandle)
{
    HAL_StatusTypeDef err = HAL_ERROR;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    if (uartHandle->Instance == USART1) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
        PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_SYSCLK;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
            Error_Handler();
        }

        // USART1 clock enable
        __HAL_RCC_USART1_CLK_ENABLE();

        __HAL_RCC_GPIOA_CLK_ENABLE();
        // USART1 GPIO Configuration
        // PA9     ------> USART1_TX
        // PA10     ------> USART1_RX
        GPIO_InitStruct.Pin = SENSOR1_TX_INT0_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(SENSOR1_TX_INT0_GPIO_Port, &GPIO_InitStruct);

        GPIO_InitStruct.Pin = SENSOR1_RX_INT1_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART1;
        HAL_GPIO_Init(SENSOR1_RX_INT1_GPIO_Port, &GPIO_InitStruct);


        // USART1 DMA Init
        __HAL_RCC_GPDMA1_CLK_ENABLE();


        // GPDMA1_REQUEST_USART1_TX Init
        handle_GPDMA1_Channel6.Instance = GPDMA1_Channel6;
        handle_GPDMA1_Channel6.Init.Request = GPDMA1_REQUEST_USART1_TX;
        handle_GPDMA1_Channel6.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        handle_GPDMA1_Channel6.Init.Direction = DMA_MEMORY_TO_PERIPH;
        handle_GPDMA1_Channel6.Init.SrcInc = DMA_SINC_INCREMENTED;
        handle_GPDMA1_Channel6.Init.DestInc = DMA_DINC_FIXED;
        handle_GPDMA1_Channel6.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel6.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel6.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel6.Init.SrcBurstLength = 1;
        handle_GPDMA1_Channel6.Init.DestBurstLength = 1;
        handle_GPDMA1_Channel6.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
        handle_GPDMA1_Channel6.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel6.Init.Mode = DMA_NORMAL;

        err = HAL_DMA_Init(&handle_GPDMA1_Channel6);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_Init", err);
        }

        __HAL_LINKDMA(uartHandle, hdmatx, handle_GPDMA1_Channel6);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel6, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_ConfigChannelAttributes", err);
        }

        HAL_NVIC_SetPriority(GPDMA1_Channel6_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel6_IRQn);


        // GPDMA1_REQUEST_USART1_RX Init
        DMA_NodeConfTypeDef NodeConfig = {0};
        NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
        NodeConfig.Init.Request = GPDMA1_REQUEST_USART1_RX;
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
        err = HAL_DMAEx_List_BuildNode(&NodeConfig, &Node_GPDMA1_Channel7);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_BuildNode", err);
        }

        err = HAL_DMAEx_List_InsertNode(&List_GPDMA1_Channel7, NULL, &Node_GPDMA1_Channel7);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_InsertNode", err);
        }

        err = HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel7);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_InsertNode", err);
        }

        handle_GPDMA1_Channel7.Instance = GPDMA1_Channel7;
        handle_GPDMA1_Channel7.InitLinkedList.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel7.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
        handle_GPDMA1_Channel7.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
        handle_GPDMA1_Channel7.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel7.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
        err = HAL_DMAEx_List_Init(&handle_GPDMA1_Channel7);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_Init", err);
        }

        err = HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel7, &List_GPDMA1_Channel7);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMAEx_List_LinkQ", err);
        }

        __HAL_LINKDMA(uartHandle, hdmarx, handle_GPDMA1_Channel7);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel7, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_1, "HAL_DMA_ConfigChannelAttributes",
                    err);
        }
        HAL_NVIC_SetPriority(GPDMA1_Channel7_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel7_IRQn);

        // USART1 interrupt Init
        HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART1_IRQn);

    } else if (uartHandle->Instance == USART2) {
        // Initializes the peripherals clock
        PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2;
        PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_SYSCLK;
        if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) {
            Error_Handler();
        }

        // USART2 clock enable
        __HAL_RCC_USART2_CLK_ENABLE();

        __HAL_RCC_GPIOA_CLK_ENABLE();
        // USART2 GPIO Configuration
        // PA2     ------> USART2_TX
        // PA3     ------> USART2_RX
        GPIO_InitStruct.Pin = SENSOR2_TX_INT0_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(SENSOR2_TX_INT0_GPIO_Port, &GPIO_InitStruct);
#if 1
        GPIO_InitStruct.Pin = SENSOR2_RX_INT1_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(SENSOR2_RX_INT1_GPIO_Port, &GPIO_InitStruct);
#else   // Debug
        __HAL_RCC_GPIOD_CLK_ENABLE();
        GPIO_InitStruct.Pin = SENSORBODY_MOSI_Pin;
        GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
        GPIO_InitStruct.Pull = GPIO_NOPULL;
        GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
        GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
        HAL_GPIO_Init(SENSORBODY_MOSI_GPIO_Port, &GPIO_InitStruct);
#endif

        // USART2 DMA Init
        __HAL_RCC_GPDMA1_CLK_ENABLE();


        // GPDMA1_REQUEST_USART2_TX Init
        handle_GPDMA1_Channel8.Instance = GPDMA1_Channel8;
        handle_GPDMA1_Channel8.Init.Request = GPDMA1_REQUEST_USART2_TX;
        handle_GPDMA1_Channel8.Init.BlkHWRequest = DMA_BREQ_SINGLE_BURST;
        handle_GPDMA1_Channel8.Init.Direction = DMA_MEMORY_TO_PERIPH;
        handle_GPDMA1_Channel8.Init.SrcInc = DMA_SINC_INCREMENTED;
        handle_GPDMA1_Channel8.Init.DestInc = DMA_DINC_FIXED;
        handle_GPDMA1_Channel8.Init.SrcDataWidth = DMA_SRC_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel8.Init.DestDataWidth = DMA_DEST_DATAWIDTH_BYTE;
        handle_GPDMA1_Channel8.Init.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel8.Init.SrcBurstLength = 1;
        handle_GPDMA1_Channel8.Init.DestBurstLength = 1;
        handle_GPDMA1_Channel8.Init.TransferAllocatedPort = DMA_SRC_ALLOCATED_PORT0|DMA_DEST_ALLOCATED_PORT0;
        handle_GPDMA1_Channel8.Init.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel8.Init.Mode = DMA_NORMAL;

        err = HAL_DMA_Init(&handle_GPDMA1_Channel8);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_Init", err);
        }

        __HAL_LINKDMA(uartHandle, hdmatx, handle_GPDMA1_Channel8);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel8, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_ConfigChannelAttributes", err);
        }

        HAL_NVIC_SetPriority(GPDMA1_Channel8_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel8_IRQn);


        // GPDMA1_REQUEST_USART1_RX Init
        DMA_NodeConfTypeDef NodeConfig = {0};
        NodeConfig.NodeType = DMA_GPDMA_LINEAR_NODE;
        NodeConfig.Init.Request = GPDMA1_REQUEST_USART2_RX;
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
        err = HAL_DMAEx_List_BuildNode(&NodeConfig, &Node_GPDMA1_Channel9);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMAEx_List_BuildNode", err);
        }

        err = HAL_DMAEx_List_InsertNode(&List_GPDMA1_Channel9, NULL, &Node_GPDMA1_Channel9);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMAEx_List_InsertNode", err);
        }

        err = HAL_DMAEx_List_SetCircularMode(&List_GPDMA1_Channel9);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMAEx_List_InsertNode", err);
        }

        handle_GPDMA1_Channel9.Instance = GPDMA1_Channel9;
        handle_GPDMA1_Channel9.InitLinkedList.Priority = DMA_LOW_PRIORITY_LOW_WEIGHT;
        handle_GPDMA1_Channel9.InitLinkedList.LinkStepMode = DMA_LSM_FULL_EXECUTION;
        handle_GPDMA1_Channel9.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
        handle_GPDMA1_Channel9.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
        handle_GPDMA1_Channel9.InitLinkedList.LinkedListMode = DMA_LINKEDLIST_CIRCULAR;
        err = HAL_DMAEx_List_Init(&handle_GPDMA1_Channel9);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMAEx_List_Init", err);
        }

        err = HAL_DMAEx_List_LinkQ(&handle_GPDMA1_Channel9, &List_GPDMA1_Channel9);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMAEx_List_LinkQ", err);
        }

        __HAL_LINKDMA(uartHandle, hdmarx, handle_GPDMA1_Channel9);

        err = HAL_DMA_ConfigChannelAttributes(&handle_GPDMA1_Channel9, DMA_CHANNEL_NPRIV);
        if (err != HAL_OK) {
            LOG_HAL_ERR_MSG(Port::PORT_2, "HAL_DMA_ConfigChannelAttributes",
                    err);
        }
        HAL_NVIC_SetPriority(GPDMA1_Channel9_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(GPDMA1_Channel9_IRQn);


        // USART2 interrupt Init
        HAL_NVIC_SetPriority(USART2_IRQn, 5, 0);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    }
}

void Uart::MspDeinitCallback(UART_HandleTypeDef *uartHandle)
{
    if(uartHandle->Instance==USART1)
     {
        // Peripheral clock disable
       __HAL_RCC_USART1_CLK_DISABLE();

       // USART1 GPIO Configuration
       // PA9     ------> USART1_TX
       // PA10    ------> USART1_RX

       HAL_GPIO_DeInit(SENSOR1_TX_INT0_GPIO_Port, SENSOR1_TX_INT0_Pin);
       HAL_GPIO_DeInit(SENSOR1_RX_INT1_GPIO_Port, SENSOR1_RX_INT1_Pin);

       HAL_DMA_DeInit(&handle_GPDMA1_Channel6);
       HAL_DMA_DeInit(&handle_GPDMA1_Channel7);

       // USART1 interrupt Deinit
       HAL_NVIC_DisableIRQ(USART1_IRQn);
     }
     else if (uartHandle->Instance == USART2) {

        // Peripheral clock disable
        __HAL_RCC_USART2_CLK_DISABLE();

        //USART2 GPIO Configuration
        //PA2     ------> USART2_TX
        //PA3     ------> USART2_RX
        HAL_GPIO_DeInit(SENSOR2_TX_INT0_GPIO_Port, SENSOR2_TX_INT0_Pin);
#if 1
        HAL_GPIO_DeInit(SENSOR2_RX_INT1_GPIO_Port, SENSOR2_RX_INT1_Pin);
#else   // Debug
        HAL_GPIO_DeInit(SENSORBODY_MOSI_GPIO_Port, SENSORBODY_MOSI_Pin);
#endif

        HAL_DMA_DeInit(&handle_GPDMA1_Channel8);
        HAL_DMA_DeInit(&handle_GPDMA1_Channel9);

        // USART2 interrupt Deinit
        HAL_NVIC_DisableIRQ(USART2_IRQn);
    }
}

void Uart::TxCpltCallback(UART_HandleTypeDef *uartHandle)
{
    Uart &instance = FindInstance(uartHandle);

    if (instance.mUartCpltHandler != nullptr) {
        instance.mUartCpltHandler->transmitCplt(&instance, true);
    }
    instance.mTxBusy = false;
}

void Uart::RxCpltCallback(UART_HandleTypeDef *uartHandle)
{
    // Called when the cyclic DMA buffer turned around
    // Ignore it
}

void Uart::ErrorCallback(UART_HandleTypeDef *uartHandle)
{
    Uart &instance = FindInstance(uartHandle);
    // HAL_UART_ERROR_FE and HAL_UART_ERROR_ORE is OK when baudrate is changed
    LOG_UART_ERR_MSG(instance.mPort, "", 0);

    if (instance.mUartCpltHandler != nullptr && instance.mTxBusy) {
        instance.mUartCpltHandler->transmitCplt(&instance, false);
    }
    instance.mTxBusy = false;

    // Restart reception
    if (instance.mIsReceptionActive) {
        if (!HAL_IS_BIT_SET(instance.mUartHandle.Instance->CR3, USART_CR3_DMAR)) {
            HAL_UART_DMAStop(&instance.mUartHandle);
            instance.mLastReadPos = 0;

            HAL_UART_ReceiverTimeout_Config(uartHandle, 100);
            HAL_UART_Receive_DMA(&instance.mUartHandle, instance.mpRxBuffer, instance.mRxBufferSize);
            HAL_UART_EnableReceiverTimeout(uartHandle);
        }
    }
}


} // namespace Driver
