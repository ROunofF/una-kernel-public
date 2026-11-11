/**
 ******************************************************************************
 * @file    Pmic.hpp
 * @brief   Class for configuring PMIC to suit project needs.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_PMIC_HPP
#define __COMPONENT_PMIC_HPP

#include <cstdint>
#include <cstdbool>

#include "IGpio.hpp"
#include "PCA9420.hpp"


class Pmic : virtual public Interface::IExti::Handler {

public:

    /**
     * @brief   ASYS input source selection.
     */
    enum class AsysInput : uint8_t {
        VBAT_VIN = 0,   ///< ASYS powered from both VBAT and VIN
        VBAT     = 1,   ///< ASYS powered from VBAT only
    };

    /**
     * @brief   PMIC interrupt flags (hardware-agnostic).
     */
    struct IrqFlags {
        bool vinChanged          : 1;  ///< VIN power source change detected
        bool chargeStatusChanged : 1;  ///< Charge status changed (CHG_OK)

        IrqFlags() : vinChanged(false),
                     chargeStatusChanged(false) { }
    };

    /**
     * @brief   Get Pmic instance.
     * @retval  Pmic instance.
     */
    static Pmic& GetInstance()
    {
        static Pmic sInstance;
        return sInstance;
    }

    /**
     * @brief   Configure chip with I2C driver.
     * @param   i2c: I2C bus.
     * @param   irq: Interrupt pin.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool config(Interface::II2c &i2c, Interface::IExti &irq, uint8_t devAddr =
            Hardware::PCA9420::skI2cAddr)
    {
        mpIrq = &irq;
        return mPCA9420.config(&i2c, devAddr);
    }

    /**
     * @brief   Initialize PMIC component.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool init();

    /**
     * @brief   Check if USB detected.
     * @retval  'true' USB detected, 'false' otherwise.
     */
    bool isUsbDetected();

    /**
     * @brief   Check if battery detected.
     * @retval  'true' battery detected, 'false' otherwise.
     */
    bool isBatteryDetected();

    /**
     * @brief   Check if battery is charging.
     * @retval  'true' battery is charging, 'false' otherwise.
     */
    bool isCharging();

    /**
     * @brief   Set charge current limit.
     * @param   mA: current limit. Max 315 mA.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setChargeCurrent(uint16_t mA);


    /**
     * @brief   PowerOFF
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool powerOff();

    /**
     * @brief   Check if charger is in DONE state.
     * @retval  'true' if charger completed charging cycle, 'false' otherwise.
     */
    bool isChargeDone();

    /**
     * @brief   Enable or disable the battery charger.
     * @param   enable: true = enable, false = disable.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setChargeEnable(bool enable);

    /**
     * @brief   Set ASYS input source.
     * @param   source: ASYS input source AsysInput.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool setAsysInputSource(AsysInput source);

    /**
     * @brief   Read and clear PMIC interrupt flags.
     * @note    Do not call this from interrupt context!
     * @retval  IrqFlags with active interrupt flags before clearing.
     */
    IrqFlags clearIrq();

    /**
     * @brief   PMIC IRQ Handler.
     */
    class IrqHandler {
    public:
        /**
         * @brief   External interrupt handler.
         */
        virtual void onIrq() = 0;
    };

    /**
     * @brief   Set PMIC interrupt handler.
     * @param   pHandler: Irq handler IrqHandler.
     */
    virtual void setIrqHandler(IrqHandler *pHandler)
    {
        mpIrqHandler = pHandler;
    }

    /**
     * @brief Checks whether the IRQ line is asserted.
     *
     * The IRQ signal is active-low, so the line is considered asserted
     * when the GPIO input level is low.
     *
     * @return true if the IRQ line is active/asserted, false otherwise.
     */
    bool isIrqAsserted();

protected:

    /**
     * @brief   Constructor.
     */
    Pmic() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Pmic() = default;

private:

    // Hardware components
    Hardware::PCA9420 mPCA9420;;
    Interface::IExti *mpIrq = nullptr;
    IrqHandler *mpIrqHandler = nullptr;

    // Driver found on I2C bus
    bool mIsPresent = false;

    bool mInited = false;

    /**
     * @brief   Decide whether the PMIC powered up only because a battery
     *          was attached, with no USB and no intentional reset/wake
     *          event. Such a start should not boot the watch.
     * @retval  'true' if the device should be parked in ship mode.
     */
    bool shouldEnterShipMode();

    /**
     * @brief   PMIC interrupt handler.
     * @param   pExti: pointer to the external interrupt instance IExti.
     * @param   edge: Interrupt edge Edge.
     */
    virtual void extiHandler(Interface::IExti *pExti,
            Interface::IExti::Edge edge) override;

};

#endif /* __COMPONENT_PMIC_HPP */
