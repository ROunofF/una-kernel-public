/**
 ******************************************************************************
 * @file    Wdg.hpp
 * @brief   Watchdog driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __DRIVER_WDG_HPP_
#define __DRIVER_WDG_HPP_

#include <cstdint>
#include <cstdbool>

#include "main.h"
#include "IWdg.hpp"


namespace Driver
{

/**
 * @brief   Watchdog driver.
 */
class Wdg : public Interface::IWdg {

public:

    /**
     * @brief   Get driver instance.
     * @retval  Driver instance.
     */
    static Wdg& GetInstance()
    {
        static Wdg sInstance;
        return sInstance;
    }

    /**
     * @brief   Start or reconfigure time watchdog.
     * @param   ms: Time 0 - 32760 ms.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool start(uint32_t ms) override;

    /**
     * @brief   Check if Watchdog is running.
     */
    virtual bool isRun() override;

    /**
     * @brief   Update watchdog.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool update() override;

private:

    // HAL WDG object
    IWDG_HandleTypeDef mWdgHandle { };

    // Indicates that WDG is running
    bool mIsRunning = false;

    /**
     * @brief   Constructor.
     */
    Wdg() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Wdg() = default;

    /**
     * @brief   Get WDG configuration parameters depend on time.
     * @param   ms: Maximum update time;
     * @param   prescaller: Reference to save prescaller.
     * @param   reload: Reference to save reload value.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool getConfig(uint32_t ms, uint32_t &prescaller, uint16_t &reload);

};

} /* namespace Driver */

#endif /* __DRIVER_WDG_HPP_ */
