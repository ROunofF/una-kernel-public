/**
 ******************************************************************************
 * @file    IBleThreadWork.hpp
 * @brief   Port for running a piece of work on the BLE event thread.
 ******************************************************************************
 *
 * The BlueNRG event thread is the single context in which GATT-client state may
 * be mutated and in which blocking ACI commands may be issued. Work that
 * originates elsewhere - notably one-shot timer callbacks, which run on the
 * shared RTOS timer-daemon task - must be marshalled onto that thread rather
 * than touching client state or the controller directly. This port provides
 * that hop; the BLE core implements it and drains the posted work from its
 * event loop.
 *
 ******************************************************************************
 */

#ifndef BLE_IBLE_THREAD_WORK_HPP
#define BLE_IBLE_THREAD_WORK_HPP

namespace Ble
{

/**
 * @brief   Runs a callback on the BLE event thread.
 */
class IBleThreadWork {
public:

    /**
     * @brief   Post a callback to run on the BLE event thread.
     * @param   fn: callback to run; invoked once on the BLE event thread.
     * @param   ctx: opaque context passed to the callback.
     * @retval  'true' if queued, 'false' if the work queue is full.
     * @note    Thread-safe: may be called from any thread.
     * @note    'fn' must have static storage duration and 'ctx' must point to an
     *          object of static lifetime (a singleton). The call may be serviced
     *          after the posting context is gone, so nothing transient may be
     *          captured by either.
     */
    virtual bool post(void (*fn)(void*), void* ctx) = 0;

protected:
    virtual ~IBleThreadWork() = default;
};

} // namespace Ble

#endif // BLE_IBLE_THREAD_WORK_HPP
