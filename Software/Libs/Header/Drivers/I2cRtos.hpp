/**
 ******************************************************************************
 * @file    I2cRtos.hpp
 * @brief   Thread-safe wrapper for I2C driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef DRIVER_I2C_RTOS_HPP
#define DRIVER_I2C_RTOS_HPP

#include <cstdint>
#include <cstdbool>

#include "II2c.hpp"

#include "FreeRTOS.h"
#include "cmsis_os2.h"

namespace Driver
{

/**
 * @brief   Thread-safe wrapper for I2C driver.
 */
class I2cRtos : virtual public Interface::II2c,
        virtual public Interface::II2c::CpltHandler
{

public:

    /**
     * @brief   Constructor.
     */
    I2cRtos() = default;

    /**
     * @brief   Constructor.
     * @param   pObj: Object access to which must be protected.
     * @param   timeout: Maximum access waiting time.
     */
    I2cRtos(Interface::II2c *pObj, uint32_t timeout);

    /**
     * @brief   Destructor.
     */
    virtual ~I2cRtos();

    /**
     * @brief   Configure.
     * @param   pObj: Object access to which must be protected.
     * @param   timeout: Maximum access waiting time.
     */
    void config(Interface::II2c *pObj, uint32_t timeout);

    // II2c interface - thread-safe wrappers around the guarded object.

    virtual bool init() override;

    virtual void deinit() override;

    virtual bool transmit(uint8_t devAddr, const uint8_t *pData, uint16_t size,
            bool blocking = true) override;

    virtual bool receive(uint8_t devAddr, uint8_t *pData, uint16_t size,
            bool blocking = true) override;

    virtual bool writeMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
            const uint8_t *pData, uint16_t size, bool blocking = true) override;

    virtual bool readMem(uint8_t devAddr, uint16_t memAddr, uint8_t memAddrSize,
            uint8_t *pData, uint16_t size, bool blocking = true) override;

    virtual bool isDeviceReady(uint8_t devAddr) override;

    virtual bool isBusBusy() override;

    virtual bool recover() override;

    /**
     * @brief   Configure bus-free wait parameters.
     *          Used to recover from a slave that holds SDA low while it
     *          loses power (e.g. STUSB4500 on USB disconnect). Disabled
     *          by default; enable per bus where the situation can occur.
     * @param   maxWaitMs: total timeout to wait for the bus to become idle.
     *                     Set to 0 to disable the wait (the default).
     * @param   stepMs:    poll interval (osDelay step) while waiting.
     *                     Default 5 ms.
     * @param   graceMs:   settling delay applied AFTER the BUSY flag has
     *                     cleared, only when waiting actually occurred.
     *                     The dying slave may briefly re-pull SDA right
     *                     after release; this gives it time to fully die.
     *                     Default 10 ms.
     */
    void setBusFreeWait(uint32_t maxWaitMs, uint32_t stepMs = 5,
                        uint32_t graceMs = 10);

private:

    // Mutex
    osMutexId_t       mMutHandle = nullptr;
    StaticSemaphore_t mMutControlBlock {};

    // Protected object
    Interface::II2c *mpObj = nullptr;

    // Access waiting time
    uint32_t mTimeout = 0;

    // Action complete successful flag
    static const uint32_t skCpltNotifyFlag = 1;

    // Action failed flag
    static const uint32_t skErrNotifyFlag = 2;

    // The thread that called the method and is waiting for it to complete
    osThreadId_t mCallingThread = nullptr;

    // Bus-free wait parameters (see setBusFreeWait()).
    // Disabled by default — opt-in per bus (only the main bus needs it,
    // because STUSB4500 there is powered from VBUS and holds SDA low for
    // up to ~190 ms during die-out after USB disconnect).
    uint32_t mBusFreeMaxWaitMs = 0;
    uint32_t mBusFreeStepMs    = 5;
    uint32_t mBusFreeGraceMs   = 10;

    /**
     * @brief   Non-blocking wait until the bus releases (BUSY flag clears)
     *          or the configured timeout expires.
     */
    void waitBusFree();

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
     * @brief   Wait for the started transfer to complete and recover the
     *          peripheral if it could not start or timed out.
     * @param   started: result of the underlying transfer start call.
     * @retval  'true' transfer completed successfully, 'false' otherwise.
     */
    bool finishAction(bool started);

    /**
     * @brief   Clear context when transfer action is done.
     */
    void actionDone(bool status);

    /**
     * @brief   Set transfer complete interrupt handler.
     * @param   pHandler: interrupt handler II2c::CpltHandler.
     */
    virtual void setCpltHandler(Interface::II2c::CpltHandler *pHandler) override
    {
        // Unused: this wrapper registers itself as the handler in config().
    }

    /**
     * @brief   Transfer complete interrupt handler
     * @param   pI2c: pointer to the II2c instance.
     * @param   status: 'true' - success, 'false' - otherwise.
     */
    virtual void transferCplt(Interface::II2c *pI2c, bool status) override;


};

} // namespace Driver

#endif // DRIVER_I2C_RTOS_HPP
