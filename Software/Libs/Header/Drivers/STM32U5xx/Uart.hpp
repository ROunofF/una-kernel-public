/**
 ******************************************************************************
 * @file    Uart.hpp
 * @brief   UART driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef DRIVER_UART_HPP
#define DRIVER_UART_HPP

#include "IUart.hpp"
#include "main.h"

namespace Driver
{

/**
 * @brief   UART driver.
 */
class Uart : public virtual Interface::IUart {

public:

    /**
     * @brief   Available ports.
     */
    enum class Port : uint8_t {
        PORT_1 = 0,
        PORT_2,

        PORT_NUM    // Not a valid parameter
    };

    /**
     * @brief   Get driver instance.
     * @param   port: target port.
     * @retval  Driver instance.
     */
    static Uart& GetInstance(Port port);

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
     * @copydoc Interface::IUart::abortRx().
     */
    virtual void abortRx() override;

    /**
     * @copydoc Interface::IUart::flushRx()
     */
    virtual void flushRx() override;

    /**
     * @copydoc Interface::IUart::setBuffer()
     */
    virtual void setBuffer(void *pBuff, size_t size) override;


    virtual bool setBaudrate(uint32_t baudrate) override;

    /**
     * @brief   TX-targeted recovery: abort a wedged DMA transmit and clear
     *          the busy flag, without disturbing the continuous RX reception.
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

    // Interrupt handlers for this driver.
    // Call these from the matching ISRs in stm32u5xx_it.c.
    static void IrqHandlerPort1();
    static void IrqDmaCh6HandlerPort1();
    static void IrqDmaCh7HandlerPort1();

    static void IrqHandlerPort2();
    static void IrqDmaCh8HandlerPort2();
    static void IrqDmaCh9HandlerPort2();


private:

    // Port number
    Port mPort {};

    // HAL UART object
    UART_HandleTypeDef mUartHandle {};

    // Indicates that the UART port is initialized
    bool mIsInitialized = false;

    // Transfer complete interrupt handler
    Interface::IUart::CpltHandler *mUartCpltHandler = nullptr;

    // Baudrate
    uint32_t mBaudrate = 115200;


    static constexpr uint32_t skRxBufferSize = 2048;

    uint8_t mRxBuff[skRxBufferSize] __attribute__((aligned(32)));
    uint8_t *mpRxBuffer = mRxBuff;
    uint16_t mRxBufferSize = sizeof(mRxBuff);

    bool mIsReceptionActive = false;
    uint32_t mLastReadPos = 0; // Last read position

    bool mTxBusy = false;



    /**
     * @brief   Constructor.
     */
    Uart() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Uart()
    {
        deinit();
    }

    /**
     * @brief   Find instance by UART handle.
     * @param   uartHandle: HAL UART Handle.
     * @retval  Driver instance.
     */
    static Uart& FindInstance(UART_HandleTypeDef *uartHandle);

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
     * @brief   Handles the UART receiver timeout.
     * @param   uart: UART driver instance.
     */
    static void RtoHandler(Uart& uart);

    /**
     * @brief Read data from a circular buffer.
     * @param pData: Pointer to data buffer.
     * @param size: The maximum amount of data to read.
     * @param The number of bytes actually read.
     */
    uint16_t receiveFromDMA(uint8_t *pData, uint16_t maxSize);

    /**
     * @brief Quick change baudrate via HAL
     * @param uartHandle: UART handle
     * @param newBaudRate: New rate
     * @retval HAL_StatusTypeDef
     */
    static HAL_StatusTypeDef SetBaudRateHAL(UART_HandleTypeDef *uartHandle,
            uint32_t newBaudRate);

    /**
     * @brief   Initialization of the UARTx instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool UARTx_Init(USART_TypeDef *pInstance);

    /**
     * @brief   Deinitialization of the UARTx instance.
     */
    void UARTx_DeInit();

    // HAL callbacks
    static void MspInitCallback(UART_HandleTypeDef *uartHandle);
    static void MspDeinitCallback(UART_HandleTypeDef *uartHandle);
    static void TxCpltCallback(UART_HandleTypeDef *uartHandle);
    static void RxCpltCallback(UART_HandleTypeDef *uartHandle);
    static void ErrorCallback(UART_HandleTypeDef *uartHandle);

};

} // namespace Driver

#endif // DRIVER_UART_HPP
