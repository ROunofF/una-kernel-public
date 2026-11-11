/**
 ******************************************************************************
 * @file    IWdg.hpp
 * @brief   Watchdog (WDG) timer interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_WDG_HPP
#define __INTERFACE_I_WDG_HPP

#include <stdint.h>
#include <stdbool.h>

namespace Interface
{

/**
 * @brief   Watchdog (WDG) timer interface.
 */
class IWdg {

public:

    /**
     * @brief   Start or reconfigure watchdog time.
     * @param   ms: Time in ms.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool start(uint32_t ms) = 0;

    /**
     * @brief   Stop watchdog timer.
     * @note    Usually you can't stop WDG.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    virtual bool stop()
    {
        return false;
    }

    /**
     * @brief   Check if watchdog is running.
     */
    virtual bool isRun() = 0;

    /**
     * @brief   Update software watchdog timer by resetting window during
     *          which you need to update the timer.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    virtual bool update() = 0;

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~IWdg() = default;

};

} /* namespace Interface */

#endif /* __INTERFACE_I_WDG_HPP */
