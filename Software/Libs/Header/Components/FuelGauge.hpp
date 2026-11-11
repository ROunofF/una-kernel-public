/**
 ******************************************************************************
 * @file    FuelGauge.hpp
 * @brief   Fuel Gauge.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_FUEL_GAUGE_HPP
#define __COMPONENT_FUEL_GAUGE_HPP

#include <cstdint>
#include <cstdbool>

#include "MAX17262.hpp"

class FuelGauge {

public:

    /**
     * @brief   Get FuelGauge instance.
     * @retval  FuelGauge instance.
     */
    static FuelGauge& GetInstance()
    {
        static FuelGauge sInstance;
        return sInstance;
    }

    /**
     * @brief   Configure chip with I2C driver.
     * @param   pI2c: pointer to an I2C.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool config(Interface::II2c &i2c, uint8_t devAddr =
            Hardware::MAX17262::skI2cAddr)
    {
        return mMAX17262.config(&i2c, devAddr);
    }

    /**
     * @brief   Initialize FuelGauge component.
     *
     * @note    The MAX17262 is powered either by the battery (Vsens line) or,
     *          if there is no battery, then by the PMIC (Vbat line). In the
     *          second case, the PMIC sends short pulses to Vbat to detect the
     *          presence of a battery. So, it is important to initialize the
     *          MAX17262 exactly when it is powered up.
     *
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool init();

    /**
     * @brief   Get actual battery level.
     * @retval  Actual battery level (0 - 100%).
     */
    float getBatteryLevel();

    /**
     * @brief   Get raw battery level, unclamped (getBatteryLevel clamps to
     *          100%; this may read slightly higher in the saturation zone).
     * @retval  Battery level in %.
     */
    float getRawBatteryLevel();

    /**
     * @brief   Get actual battery voltage.
     * @retval  Actual battery voltage in volts.
     */
    float getBatteryVoltage();

    /**
     * @brief   Get averaged battery voltage (immune to short load-spike dips
     *          in the instantaneous reading).
     * @retval  Averaged battery voltage in volts.
     */
    float getAvgBatteryVoltage();

    /**
     * @brief   Get actual capacity of the battery.
     * @retval  Actual capacity in mAh.
     */
    float getCapacity();

    /**
     * @brief   Get theoretical capacity of the battery.
     * @retval  Theoretical capacity in mAh.
     */
    float getFullCapacity();

    /**
     * @brief   Get actual current which flows in (or out) the battery.
     * @retval  Actual current in mA. ( >0 - in, <0 - out )
     */
    float getCurrent();

    /**
     * @brief   Get average current which flows in (or out) the battery.
     * @retval  Average current in mA. ( >0 - in, <0 - out )
     */
    float getAvgCurrent();

    /**
     * @brief   Read from the specified register
     * @retval  Register value
     */
    uint16_t readReg(uint8_t addr);

    /**
     * @brief   Whether init() found the gauge in a POR state (battery was
     *          disconnected since last boot, so the m5 learning state was
     *          lost). Sticky for this firmware session: the live hardware
     *          flag is cleared at the end of init(), so callers that arrive
     *          later still get an honest answer.
     */
    bool wasPowerOnReset() const { return mWasPowerOnReset; }

    /**
     * @brief   Read a decoded telemetry snapshot in a single I2C burst.
     * @param   t: destination telemetry.
     * @retval  'true' on success, 'false' if the gauge is absent or the read
     *          failed.
     */
    bool getTelemetry(Hardware::MAX17262::Telemetry& t);

protected:

    /**
     * @brief   Constructor.
     */
    FuelGauge() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~FuelGauge() = default;

private:

    // Hardware components
    Hardware::MAX17262 mMAX17262;

    // Driver found on I2C bus
    bool mIsPresent = false;

    bool mInited = false;

    // Captured at init(): true if the gauge was in POR state when this
    // firmware session started. Stays sticky after the live flag is cleared.
    bool mWasPowerOnReset = false;

};

#endif /* __COMPONENT_FUEL_GAUGE_HPP */
