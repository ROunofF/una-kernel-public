/**
 ******************************************************************************
 * @file    SpiRtos.hpp
 * @brief   Thread-safe RTOS wrapper for SPI driver.
 *          Internally uses DMA (non-blocking) transfers, but exposes a
 *          blocking-style API by waiting on RTOS thread flags.
 ******************************************************************************
 */

#ifndef DRIVER_SPI_RTOS_HPP
#define DRIVER_SPI_RTOS_HPP

#include <cstdint>
#include <cstdbool>

#include "ISpi.hpp"

#include "FreeRTOS.h"
#include "cmsis_os2.h"

namespace Driver
{

/**
 * @brief   Thread-safe RTOS wrapper for SPI driver.
 *
 *          Wraps any ISpi implementation. All transfers are dispatched
 *          as non-blocking (DMA) calls; the calling thread is suspended
 *          via RTOS thread flags until the transfer completes or an error
 *          is signalled by the driver callback. This allows the RTOS
 *          scheduler to run other tasks while the DMA transfer is in
 *          progress.
 *
 * Usage:
 * @code
 *   Driver::SpiRtos spiRtos(&Driver::Spi::GetInstance(Driver::Spi::Port::PORT_1), 500);
 *   spiRtos.init();
 *   spiRtos.transmit(buf, len);   // blocks calling thread, yields CPU
 * @endcode
 */
class SpiRtos : virtual public Interface::ISpi,
                virtual public Interface::ISpi::CpltHandler
{

public:

    /**
     * @brief   Default constructor. Call config() before use.
     */
    SpiRtos() = default;

    /**
     * @brief   Constructor with immediate configuration.
     * @param   pObj:    Pointer to the ISpi instance to wrap.
     * @param   timeout: Maximum time (ms) to wait for a transfer to complete.
     */
    SpiRtos(Interface::ISpi *pObj, uint32_t timeout);

    /**
     * @brief   Destructor. Releases mutex and removes callback from wrapped driver.
     */
    virtual ~SpiRtos();

    /**
     * @brief   Configure (or reconfigure) the wrapper.
     * @param   pObj:    Pointer to the ISpi instance to wrap.
     * @param   timeout: Maximum time (ms) to wait for a transfer to complete.
     */
    void config(Interface::ISpi *pObj, uint32_t timeout);

    /**
     * @brief   Initialize the wrapped SPI driver.
     * @retval  true on success, false otherwise.
     */
    virtual bool init() override;

    /**
     * @brief   Deinitialize the wrapped SPI driver.
     */
    virtual void deinit() override;

    /**
     * @brief   Set SPI mode (CPOL/CPHA). Delegated directly to the wrapped driver.
     * @param   mode: Desired SPI mode.
     */
    virtual void setMode(Interface::ISpi::Mode mode) override;

    /**
     * @brief   Transmit data over SPI.
     *          Internally uses DMA. The calling thread blocks until done.
     * @param   pData:    Pointer to data buffer to transmit.
     * @param   size:     Number of bytes to transmit.
     * @param   blocking: Parameter kept for interface compatibility; ignored
     *                    (always behaves as blocking from the caller's perspective).
     * @retval  true on success, false on error or timeout.
     */
    virtual bool transmit(const uint8_t *pData, uint16_t size,
            bool blocking = true) override;

    /**
     * @brief   Receive data over SPI.
     *          Internally uses DMA. The calling thread blocks until done.
     * @param   pData:    Pointer to buffer to store received data.
     * @param   size:     Number of bytes to receive.
     * @param   blocking: Parameter kept for interface compatibility; ignored.
     * @retval  true on success, false on error or timeout.
     */
    virtual bool receive(uint8_t *pData, uint16_t size,
            bool blocking = true) override;

    /**
     * @brief   Transmit and receive data simultaneously over SPI.
     *          Internally uses DMA. The calling thread blocks until done.
     * @param   pTxData:  Pointer to data buffer to transmit.
     * @param   pRxData:  Pointer to buffer to store received data.
     * @param   size:     Number of bytes to transfer.
     * @param   blocking: Parameter kept for interface compatibility; ignored.
     * @retval  true on success, false on error or timeout.
     */
    virtual bool transfer(uint8_t *pTxData, uint8_t *pRxData, uint16_t size,
            bool blocking = true) override;

    /**
     * @brief   Recover the wrapped SPI driver (thread-safe).
     * @retval  true on success, false otherwise.
     */
    virtual bool recover() override;

    /**
     * @brief   Set transfer complete handler. Not used by this wrapper
     *          (the wrapper registers itself as the internal handler).
     * @param   pHandler: Unused.
     */
    virtual void setCpltHandler(Interface::ISpi::CpltHandler *pHandler) override
    {
        // Intentionally unused: SpiRtos registers itself as the CpltHandler
        // of the wrapped driver. External handlers are not supported.
        (void) pHandler;
    }

private:

    // Mutex used to serialise access from multiple threads
    osMutexId_t       mMutHandle        = nullptr;
    StaticSemaphore_t mMutControlBlock  {};

    // Pointer to the wrapped ISpi driver
    Interface::ISpi  *mpObj             = nullptr;

    // Timeout in ms for both mutex acquisition and transfer completion
    uint32_t          mTimeout          = 0;

    // Thread flag bit: transfer completed successfully
    static const uint32_t skCpltNotifyFlag = (1U << 0);

    // Thread flag bit: transfer completed with error
    static const uint32_t skErrNotifyFlag  = (1U << 1);

    // Handle of the thread currently waiting for a transfer to finish
    osThreadId_t mCallingThread = nullptr;

    /**
     * @brief   Acquire the mutex. Logs and returns false on timeout.
     * @param   timeout: Max wait time in ms.
     * @retval  true if acquired, false otherwise.
     */
    bool mutexLock(uint32_t timeout);

    /**
     * @brief   Release the mutex.
     */
    void mutexRelease();

    /**
     * @brief   Record the calling thread before starting a non-blocking transfer.
     */
    void startAction();

    /**
     * @brief   Clear the calling thread record on early error (no callback expected).
     */
    void terminateAction();

    /**
     * @brief   Wait for the started transfer to complete and recover the
     *          driver if it could not start or timed out.
     * @param   started: result of the underlying transfer start call.
     * @retval  true on success, false on error or timeout.
     */
    bool finishAction(bool started);

    /**
     * @brief   Called from the ISpi callback to unblock the waiting thread.
     * @param   status: true = success, false = error.
     */
    void actionDone(bool status);

    /**
     * @brief   ISpi::CpltHandler implementation. Called from the driver ISR context.
     * @param   pSpi:   Pointer to the ISpi instance that fired the callback.
     * @param   status: true = success, false = error.
     */
    virtual void transferCplt(Interface::ISpi *pSpi, bool status) override;

};

} // namespace Driver

#endif // DRIVER_SPI_RTOS_HPP
