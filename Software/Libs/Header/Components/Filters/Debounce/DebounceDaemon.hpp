/**
 ******************************************************************************
 * @file    DebounceDaemon.hpp
 * @brief   DebounceDaemon class.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __FILTER_DEBOUNCE_DAEMON_HPP
#define __FILTER_DEBOUNCE_DAEMON_HPP

#include <cstdint>

#include "OS.hpp"
#include "Debounce.hpp"

namespace Filter
{

/**
 * @brief   Daemon thread to handle Debounce objects
 */
class DebounceDaemon
{

public:

    /**
     * @brief   Get DebounceDaemon instance.
     * @retval  DebounceDaemon instance.
     */
    static DebounceDaemon& GetInstance()
    {
        static DebounceDaemon sInstance;
        return sInstance;
    }

    /**
     * @brief   Add Debounce to the internal list.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool addObj(Debounce *pObj);

    /**
     * @brief   Delete Debounce from the internal list.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    void delObj(Debounce *pObj);

    /**
     * @brief   Notify daemon that input state has been changed.
     */
    void notify();

private:

    // Maximum number of objects supported by this component
    static const uint32_t skMaxFiltersNum = 6;

    // Instances
    Debounce *mpInstances[skMaxFiltersNum] { };

    // RTOS components
    OS::Mutex mMutex;
    OS::Thread<DebounceDaemon, 3*1024> mThread;

    static const uint32_t skNotifyFlag = 1;

    /**
     * @brief   Constructor.
     */
    DebounceDaemon();

    /**
     * @brief   Destructor.
     */
    virtual ~DebounceDaemon() = default;

    /**
     * @brief   Endless thread of the DebounceDaemon instance.
     */
    void thread();

};

} /* namespace Filter */

#endif /* __FILTER_DEBOUNCE_DAEMON_HPP */
