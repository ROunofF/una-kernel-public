/**
 ******************************************************************************
 * @file    System.hpp
 * @brief   System low level functionality.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __DRIVER_SYSTEM_HPP
#define __DRIVER_SYSTEM_HPP

#include <cstdint>
#include <cstdbool>

#include "main.h"
#include "SDK/Tools/FirmwareVersion.hpp"
#include "FwInfoBlock/FwInfoBlock.h"

/**
 * @brief   Convert reset reason enumeration to its corresponding string representation.
 *
 * @param   reason  The reset reason of type Driver::System::ResetReason.
 *                  Possible values are:
 *                  - Driver::System::ResetReason::STANDBY
 *                  - Driver::System::ResetReason::WATCHDOG
 *                  - Driver::System::ResetReason::SOFTWARE
 *                  - Driver::System::ResetReason::HARDWARE
 *
 * @return  A string representing the reset reason:
 *          - "STANDBY"  for Driver::System::ResetReason::STANDBY
 *          - "WATCHDOG" for Driver::System::ResetReason::WATCHDOG
 *          - "SOFTWARE" for Driver::System::ResetReason::SOFTWARE
 *          - "HARDWARE" for Driver::System::ResetReason::HARDWARE
 */
#define DRV_SYSTEM_RESET_TO_STR(reason) \
(reason == Driver::System::ResetReason::STANDBY ? "STANDBY" : \
    (reason == Driver::System::ResetReason::WATCHDOG ? "WATCHDOG" : \
            (reason == Driver::System::ResetReason::SOFTWARE ? "SOFTWARE" : \
                    "HARDWARE" )))

namespace Driver
{

/**
 * @brief   System driver.
 */
class System {

public:

    /**
     * @brief   Enumerates possible reset reasons for the MCU.
     */
    enum class ResetReason {
        HARDWARE = 0,   ///< Hardware-initiated reset (e.g., power-on or external reset).
        SOFTWARE,       ///< Software-triggered reset (e.g., system reset request).
        WATCHDOG,       ///< Watchdog timer reset due to timeout.
        STANDBY,        ///< Reset when waking up from standby or low-power mode.
    };

    /**
     * @brief   Structure to store the 96-bit MCU unique identifier (UID).
     */
    typedef struct {
        uint8_t bytes[12]; ///< Array holding the 96-bit unique identifier.
    } McuUid_t;

    /**
     * @brief   Base initialization of the clocks, flash ...
     * @note    This function will either complete successfully
     *          or call the ErrorHandler.
     */
    static void Init();

    /**
     * @brief   Clear reset reason flags
     */
    static void ClearResetFlags();

    /**
     * @brief   Get reset reason.
     * @return  Reset reason.
     */
    static ResetReason GetResetReason()
    {
        return mReserReason;
    }

    /**
     * @brief   Get the raw RCC->CSR reset-flag word latched at boot.
     * @return  Reset-flag bits as read before ClearResetFlags() ran.
     *
     * @note    Decoded offline (power-on / pin / brown-out / software / IWDG /
     *          WWDG / low-power). Latched in Init() so it stays available after
     *          the flags are cleared.
     */
    static uint32_t GetResetFlagsRaw()
    {
        return mResetFlagsRaw;
    }

    /**
     * @brief   Log the reset reason and every decoded RCC_CSR reset flag.
     * @note    Must be called after the logger is initialized. Logs on every
     *          boot, including power-on/brownout (HARDWARE) resets, so the
     *          cause of spontaneous reboots is always captured.
     */
    static void LogResetInfo();

    /**
     * @brief   Get milliseconds form system start.
     * @return  Timestamp in milliseconds.
     */
    static uint32_t GetTimeMs();

    /**
     * @brief   Delay.
     * @param   ms: time in milliseconds.
     */
    static void Delay(uint32_t ms);

#if (USE_SYSTEM_SLEEP == 1)
    /**
     * @brief   Put system to sleep.
     */
    static void Sleep();

    /**
     * @brief   Restore system after sleep.
     * @note    Call this method after the time has been adjusted after sleep.
     * @note    This function will either complete successfully or call the
     *          ErrorHandler.
     */
    static void Wake();
#endif // (USE_SYSTEM_SLEEP == 1)

    /**
     * @brief   System reset.
     */
    static void Reset();

    /**
     * @brief   Get MCU 96-bit UID.
     * @return  Reference to the MCU's unique identifier.
     */
    static const McuUid_t& GetMcuUid();

    /**
     * @brief   Get MCU 96-bit UID as string.
     * @return  Pointer to a null-terminated string containing the MCU's unique
     *          identifier.
     */
    static const char* GetMcuUidStr();

    /**
     * @brief   Get Device ID as string.
     * @return  Pointer to a null-terminated string containing the device ID.
     */
    static const char* GetDeviceIdStr();

    /**
     * @brief   Get Manufacturer Name as string.
     * @return  Pointer to a null-terminated string containing the
     *          manufacturer's name.
     */
    static const char* GetManufacturerNameStr();

    /**
     * @brief   Get Model Name as string.
     * @return  Pointer to a null-terminated string containing the model name.
     */
    static const char* GetModelNameStr();

    /**
     * @brief   Get Build Version as string.
     * @return  Pointer to a null-terminated string containing the build version.
     */
    static const char* GetBuildVersionStr();

    /**
     * @brief   Get Project name.
     * @return  Pointer to a null-terminated string containing the project name.
     */
    static const char* GetProjectName();

    /**
     * @brief   Request to Enable/Disable HSE clock.
     * @note    This function enables HSE immediately, and disables it only
     *          when everyone who previously enabled it calls this method with
     *          the parameter 'false'
     * @param   enable: requested state 'true' - enable, 'false' - disable.
     * @return  Execution status. 'true' - success, 'false' - otherwise.
     */
    static bool RequestHSE(bool enable);

    /**
     * @brief   Get LIBC version as a structure.
     * @return  Firmware Version structure.
     */
    static SDK::FirmwareVersion GetLibcVersion();

    /**
     * @brief   Get LIBC version as a string.
     * @return  Pointer to a null-terminated string containing the LIBC version.
     */
    static const char* GetLibcVersionStr();

    /**
     * @brief   Get bootloader version as a structure.
     * @return  Firmware Version structure.
     */
    static SDK::FirmwareVersion GetBootVersion();

    /**
     * @brief   Get bootloader version as a string.
     * @return  Pointer to a null-terminated string containing the bootloader version.
     */
    static const char* GetBootVersionStr();

private:

    /// Indicates that the driver is initialized
    static bool mIsInitialized;

    /// MCU Reset reason
    static ResetReason mReserReason;

    /// Raw RCC->CSR reset-flag word latched at boot (before clear)
    static uint32_t mResetFlagsRaw;

    /// HSE active consumer counter
    static uint32_t mCounterHSE;

    /**
     * @brief Constructor.
     */
    System() = default;

    /**
     * @brief Destructor.
     */
    virtual ~System() = default;

    /**
     * @brif    Configure the system clock
     */
    static void ClockConfig160Mhz();

    /**
     * @brief   Restore the 160 MHz system clock after a STOP2 exit.
     * @detail  Called from the STOP2 wake path with interrupts masked and the
     *          SysTick suspended, where HAL_GetTick() is frozen. Uses LL
     *          accessors with bounded (tick-independent) waits, so a stuck
     *          oscillator/PLL cannot hang forever: on a timed-out wait it
     *          asserts (synchronous fault -> full crash dump, not masked by
     *          PRIMASK) instead of spinning until the watchdog fires. Only
     *          restores what STOP2 tears down (VOS/booster, PLL1, flash latency,
     *          SYSCLK switch); LSE/LSI/MSIK and the RCC config registers are
     *          retained.
     */
    static void WakeRestoreClock();

};

} /* namespace Driver */

#endif /* __DRIVER_SYSTEM_HPP */
