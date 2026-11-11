/**
 ******************************************************************************
 * @file    Vibro.hpp
 * @brief   Vibro Driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_VIBRO_HPP
#define __COMPONENT_VIBRO_HPP

#include <cstdint>
#include <cstdbool>

#include "IVibro.hpp"
#include "II2c.hpp"
#include "IGpio.hpp"
#include "DRV2625.hpp"
#include "OS.hpp"

class Vibro : public Interface::IVibro {

public:

    /**
     * @brief   Get Vibro instance.
     * @retval  Vibro instance.
     */
    static Vibro& GetInstance()
    {
        static Vibro sInstance;
        return sInstance;
    }

    /**
     * @brief   Configure chip with I2C driver.
     * @param   i2c: I2C bus.
     * @param   rst: Reset pin.
     * @param   devAddr: Target device address. 7 bit, not shifted.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool config(Interface::II2c &i2c, Interface::IGpo &rst, uint8_t devAddr =
            Hardware::DRV2625::skI2cAddr)
    {
        mpRst = &rst;
        return mDRV2625.config(&i2c, devAddr);
    }

    /**
     * @brief   Initialize vibro motor.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool init();

    /**
     * @brief   Deinitialization of the vibro motor instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool deinit();

    /**
     * @brief   Turn on vibro.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool enable();

    /**
     * @brief   Turn off vibro and put it to low power mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool disable();

    /**
     * @brief   Check if the vibro is on.
     * @retval  'true' - vibro is on, 'false' - otherwise.
     */
    bool isEnabled();

    /**
     * @brief   Play effect.
     * @note    Turn on vibro before play.
     * @note    Commands are queued, so you can call this method without delay.
     * @param   effect: Effect to play;
     */
    virtual void play(uint8_t effect = Effect::STRONG_CLICK_100) override;

    /**
     * @brief   Play melody.
     * @note    Turn on vibro before play.
     * @param   melody: array of effects. Maximum 8 notes includes pauses.
     * @param   len: length of the array. (Max len is skEffectQueueSize)
     * @param   loop: main loop 1-6 times, 0 - no repeat, 7 - infinity loop.
     */
    virtual void play(const Note melody[], uint8_t len, uint8_t loop = 0) override;

    /**
     * @brief   Check whether the vibro is playing.
     * @retval  'true' if playing, 'false' otherwise.
     */
    virtual bool isPlaying() override;

    /**
     * @brief   Stop playing melody.
     */
    virtual void stop() override;

private:

    // DRV2625 chip is OK and initialized
    bool mIsInit = false;

    // Vibro is active
    bool mIsOn = false;

    Interface::IGpo *mpRst = nullptr;
    Hardware::DRV2625 mDRV2625;

    /**
     * @brief   Constructor.
     */
    Vibro() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Vibro() = default;

    // RTOS components
    OS::Mutex mMutex;
    static const uint32_t skMutexTimeout = 1000;

    /**
     * @brief   Read config for DRV2625 from device memory.
     * @param   bemfGain: the analog gain of the back-EMF amplifier.
     * @param   calComp: the voltage-compensation result after execution
     *          of the calibration routine.
     * @param   calBmf: the feedback gain for closed-loop operation.
     * @retval  Execution status. 'true' if success, 'false' config not
     *          present, or reading failed.
     */
    bool getConfig(uint8_t &bemfGain, uint8_t &calComp, uint8_t &calBmf);

    /**
     * @brief   Save config for DRV2625 to device memory.
     * @param   bemfGain: the analog gain of the back-EMF amplifier.
     * @param   calComp: the voltage-compensation result after execution
     *          of the calibration routine.
     * @param   calBmf: the feedback gain for closed-loop operation.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool saveConfig(uint8_t bemfGain, uint8_t calComp, uint8_t calBmf);

};

#endif /* __COMPONENT_VIBRO_HPP */
