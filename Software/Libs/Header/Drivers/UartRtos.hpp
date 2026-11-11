/**
 ******************************************************************************
 * @file    UartRtos.hpp
 * @brief   RTOS wrapper for the Uart driver that allows you to work with the
 *          driver in blocking mode, using context switching to other tasks.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef DRIVER_UART_RTOS_HPP
#define DRIVER_UART_RTOS_HPP

#include <stdint.h>
#include <stdbool.h>

#include "IUart.hpp"

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "stream_buffer.h"

namespace Driver
{

/**
 * @brief   RTOS wrapper for Uart driver.
 */
class UartRtos : virtual public Interface::IUart,
        virtual public Interface::IUart::CpltHandler
{

public:

    /**
     * @brief   Constructor.
     */
    UartRtos() = default;

    /**
     * @brief   Constructor.
     * @param   obj: Object access to which must be protected.
     * @param   timeout: Maximum access waiting time.
     */
    UartRtos(Interface::IUart &obj, uint32_t timeout = 1000);

    /**
     * @brief   Destructor.
     */
    virtual ~UartRtos();

    /**
     * @brief   Configure.
     * @param   obj: Object access to which must be protected.
     * @param   timeout: Maximum access waiting time.
     */
    void config(Interface::IUart &obj, uint32_t timeout);

    // IUart interface - thread-safe wrappers around the guarded object.

    virtual bool init() override;

    virtual void deinit() override;

    virtual bool startReception() override;

    virtual bool stopReception() override;

    virtual bool isReceptionActive() override;

    virtual bool transmit(const uint8_t *pData, size_t size,
            uint32_t timeout = UINT32_MAX) override;

    virtual bool isTxBusy() override;

    virtual bool receive(uint8_t *pData, size_t size, size_t &br,
            uint32_t timeout = UINT32_MAX) override;

    virtual size_t getAvailableDataSize() override;

    virtual void abortRx() override;

    virtual void flushRx() override;

    /**
     * @brief   This allows you to set an external buffer for receiving
     *          data, instead of an internal one.
     *
     * @note    This method sets up a buffer for the stream only, so you can
     *          read data less often.
     *          The physical UART buffer remains unchanged.
     *
     * @param   pBuff: Pointer to the external buffer. If the pointer is null,
     *          then the internal buffer will be used.
     * @param   size: The size of the buffer is in bytes. If the size is 0,
     *          then the internal buffer will be used.
     */
    virtual void setBuffer(void *pBuff, size_t size) override;

    virtual bool setBaudrate(uint32_t baudrate) override;

    virtual bool recover() override;

private:

    // Mutex
    osMutexId_t mMutHandle = nullptr;
    StaticSemaphore_t mMutControlBlock {};

    // Protected object
    Interface::IUart *mpObj = nullptr;

    // Access waiting time
    uint32_t mTimeout = 0;

    // Action complete successful flag
    static const uint32_t skCpltNotifyFlag = 1;

    // Action failed flag
    static const uint32_t skErrNotifyFlag = 2;

    // The thread that called the method and is waiting for it to complete
    osThreadId_t mCallingThread = nullptr;

    StreamBufferHandle_t mStreamHandle = nullptr;
    StaticStreamBuffer_t mStreamControlBlock {};

    static constexpr uint32_t skRxInternalBufferSize = 512;
    uint8_t mRxInternalBuff[skRxInternalBufferSize];
    uint8_t *mpRxBuffer = mRxInternalBuff;
    uint32_t mRxBufferSize = sizeof(mRxInternalBuff);

    Interface::IUart::CpltHandler *mpCpltHandler = nullptr;

    bool mRxAbortFlag = false;


    /**
     * @brief   Lock access to internal data to sync transactions from
     *          different threads.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool mutexLock(uint32_t timeout);

    /**
     * @brief   Unlock access to internal data.
     */
    void mutexRelease();

    /**
     * @brief   Init context at start transfer action.
     */
    void startAction();

    /**
     * @brief   Clear context in case of error.
     */
    void terminateAction();

    /**
     * @brief   Wait for the started transmit to complete and recover the
     *          driver if it could not start or timed out.
     * @param   started: result of the underlying transmit start call.
     * @param   timeout: max wait time in ms.
     * @retval  true on success, false on error or timeout.
     */
    bool finishAction(bool started, uint32_t timeout);

    /**
     * @brief   Clear context when transfer action is done.
     */
    void actionDone(bool status);

    /**
     * @brief   Set transfer complete interrupt handler.
     * @param   pHandler: interrupt handler IUart::CpltHandler.
     */
    virtual void setCpltHandler(Interface::IUart::CpltHandler *pHandler) override
    {
        mpCpltHandler = pHandler;
    }

    /**
     * @brief   Transmit transfer complete interrupt handler.
     * @param   pUart: pointer to the IUart instance.
     * @param   status: 'true' - success, 'false' - otherwise.
     */
    virtual void transmitCplt(Interface::IUart *pUart, bool status) override;

    /**
     * @brief   Receive transfer complete interrupt handler.
     * @retval  The number of bytes available for reading.
     */
    virtual void receiveCplt(Interface::IUart *pUart, size_t size) override;


};

} // namespace Driver

#endif // DRIVER_UART_RTOS_HPP
