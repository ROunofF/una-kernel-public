/**
 ******************************************************************************
 * @file    LpUart.hpp
 * @brief   LPUART driver with STOP2 wake-up support.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef DRIVER_LPUART_HPP
#define DRIVER_LPUART_HPP

#include "IUart.hpp"
#include "main.h"

namespace Driver
{

/**
 * @brief   LPUART driver with low-power wake-up capability.
 */
class LpUart : public virtual Interface::IUart {

public:

    /**
     * @brief   Available ports.
     */
    enum class Port : uint8_t {
        PORT_1 = 0,

        PORT_NUM
    };

    /**
     * @brief   Get driver instance.
     * @param   port: target port.
     * @retval  Driver instance.
     */
    static LpUart& GetInstance(Port port);

    /**
     * @copydoc Interface::IUart::init()
     */
    virtual bool init() override;

    /**
     * @copydoc Interface::IUart::deinit()
     */
    virtual void deinit() override;

    /**
     * @copydoc Interface::IUart::startReception()
     */
    virtual bool startReception() override;

    /**
     * @copydoc Interface::IUart::stopReception()
     */
    virtual bool stopReception() override;

    /**
     * @copydoc Interface::IUart::isReceptionActive()
     */
    virtual bool isReceptionActive() override;

    /**
     * @copydoc Interface::IUart::transmit()
     */
    virtual bool transmit(const uint8_t *pData, size_t size,
            uint32_t timeout = UINT32_MAX) override;

    /**
     * @copydoc Interface::IUart::isTxBusy()
     */
    virtual bool isTxBusy() override;

    /**
     * @copydoc Interface::IUart::receive()
     */
    virtual bool receive(uint8_t *pData, size_t size, size_t &br,
            uint32_t timeout = UINT32_MAX) override;

    /**
     * @copydoc Interface::IUart::getAvailableDataSize()
     */
    virtual size_t getAvailableDataSize() override;

    /**
     * @copydoc Interface::IUart::abortRx()
     */
    virtual void abortRx() override;

    /**
     * @brief   Abort transmit DMA mode.
     */
    void abortTx();

    /**
     * @copydoc Interface::IUart::flushRx()
     */
    virtual void flushRx() override;

    /**
     * @copydoc Interface::IUart::setBuffer()
     */
    virtual void setBuffer(void *pBuff, size_t size) override;

    /**
     * @copydoc Interface::IUart::setBaudrate()
     */
    virtual bool setBaudrate(uint32_t baudrate) override;

    /**
     * @brief   TX-targeted recovery: abort a wedged DMA transmit and reset
     *          the TX state (also re-balances the sleep lock taken in
     *          transmit()), without disturbing the low-power RX reception.
     * @retval  'true' - success, 'false' - otherwise.
     */
    virtual bool recover() override;

    /**
     * @copydoc Interface::IUart::setCpltHandler()
     */
    virtual void setCpltHandler(Interface::IUart::CpltHandler *pHandler)
            override
    {
        mUartCpltHandler = pHandler;
    }

    static void IrqHandlerPort1();
    static void IrqDmaCh10HandlerPort1();
    static void IrqDmaCh11HandlerPort1();

    static void IrqHandlerTimPort1();


private:

    Port mPort {};


    UART_HandleTypeDef mUartHandle {};
    IRQn_Type mUartIrq = LPUART1_IRQn;

    // Timer to determine the end of reception to allow sleep
    TIM_HandleTypeDef mTimHandle {};
    IRQn_Type mTimIrq = TIM6_IRQn;
    bool mIsInitialized = false;

    Interface::IUart::CpltHandler *mUartCpltHandler = nullptr;

    uint32_t mBaudrate = 921600;

    static constexpr uint32_t skRxBufferSize = 128;

    uint8_t mRxBuff[skRxBufferSize] __attribute__((aligned(32)));
    uint8_t *mpRxBuffer = mRxBuff;
    uint16_t mRxBufferSize = sizeof(mRxBuff);

    bool mIsReceptionActive = false;
    uint32_t mLastReadPos = 0;



    /**
     * @brief   Constructor.
     */
    LpUart() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~LpUart()
    {
        deinit();
    }

    /**
     * @brief   Find instance by UART handle.
     * @param   uartHandle: HAL UART Handle.
     * @retval  Driver instance.
     */
    static LpUart& FindInstance(UART_HandleTypeDef *uartHandle);
    static LpUart& FindInstance(TIM_HandleTypeDef *timHandle);

    /**
     * @brief   Start reception DMA mode.
     * @param   uartHandle: HAL UART Handle.
     * @param   buff: Pointer to data buffer.
     * @param   size: Buffer size.
     */
    static void StartReception(UART_HandleTypeDef *uartHandle, uint8_t *buff, uint16_t size);

    /**
     * @brief   Stop reception DMA mode.
     * @param   uartHandle: HAL UART Handle.
     */
    static void StopReception(UART_HandleTypeDef *uartHandle);


    /**
     * @brief   Read data from circular DMA buffer.
     * @param   pData: Pointer to data buffer.
     * @param   maxSize: Maximum amount of data to read.
     * @retval  Number of bytes actually read.
     */
    uint16_t receiveFromDMA(uint8_t *pData, uint16_t maxSize);

    /**
     * @brief   Quick baudrate change via HAL.
     * @param   uartHandle: UART handle.
     * @param   newBaudRate: New baudrate.
     * @retval  HAL status.
     */
    static HAL_StatusTypeDef SetBaudRateHAL(UART_HandleTypeDef *uartHandle,
            uint32_t newBaudRate);

    /**
     * @brief   Initialize LPUARTx instance.
     * @param   pInstance: LPUART peripheral base address.
     * @param   pInstance: TIM peripheral base address.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool LPUARTx_Init(USART_TypeDef *pInstance, TIM_TypeDef* pTimInstance);

    /**
     * @brief   Deinitialize LPUART1 instance.
     */
    void LPUARTx_DeInit();

    static void MspInitCallback(UART_HandleTypeDef *uartHandle);
    static void MspDeinitCallback(UART_HandleTypeDef *uartHandle);
    static void TxCpltCallback(UART_HandleTypeDef *uartHandle);
    static void ErrorCallback(UART_HandleTypeDef *uartHandle);
    static void RxCpltCallback(UART_HandleTypeDef *uartHandle);
    static void RxEventCallback(UART_HandleTypeDef *uartHandle, uint16_t Pos);
    static void PeriodElapsedCallback(TIM_HandleTypeDef* timHandle);

    void TIMx_Init(TIM_TypeDef* pInstance);
    void TIMx_DeInit();
    static void StartIdleTimer(TIM_HandleTypeDef* timHandle);
    static void StopIdleTimer(TIM_HandleTypeDef* timHandle);
    static bool IsIdleTimerActive(TIM_HandleTypeDef* timHandle);

    enum class TxState
    {
        OFF = 0,
        TX,
    };

    TxState mTxState = TxState::OFF;

    enum class RxState
    {
        OFF = 0,
        SLEEP,
        RX,
        TIMEOUT,
    };

    RxState mRxState = RxState::OFF;
    uint16_t mDmaPosition = 0;


};

} // namespace Driver

#endif // DRIVER_LPUART_HPP
