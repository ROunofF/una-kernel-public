/**
 ******************************************************************************
 * @file    Buzzer.hpp
 * @brief   Buzzer Driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BUZZER_HPP
#define __COMPONENT_BUZZER_HPP

#include <cstdint>
#include <cstdbool>

#include "IBuzzer.hpp"
#include "IGpio.hpp"
#include "IPwm.hpp"
#include "OS.hpp"


class Buzzer : public Interface::IBuzzer {

public:

    /**
     * @brief   Get Buzzer instance.
     * @retval  Buzzer instance.
     */
    static Buzzer& GetInstance()
    {
        static Buzzer sInstance;
        return sInstance;
    }

    /**
     * @brief   Configure chip with I2C driver.
     * @param   pwm: PWM driver.
     * @param   en1: EN1 pin for level select.
     * @param   en2: EN2 pin for level select.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool config(Interface::IPwm &pwm, Interface::IGpo &en1,
            Interface::IGpo &en2)
    {
        mPwm = &pwm;
        mEn1 = &en1;
        mEn2 = &en2;
        return true;
    }

    /**
     * @brief   Initialize buzzer.
     * @retval  Execution status. 'true' if success, 'false' otherwise.
     */
    bool init();

    /**
     * @brief   Deinitialization of the Buzzer instance.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool deinit();

    /**
     * @brief   Enable/Turn on Buzzer.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool enable();

    /**
     * @brief   Disable/Turn off Buzzer or put it to low power mode.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    bool disable();

    /**
     * @brief   Check if the Buzzer is enabled.
     * @retval  'true' - Buzzer is on, 'false' - otherwise.
     */
    bool isEnabled()
    {
        return mIsEnabled;
    }

    /**
     * @brief   Play short beep.
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool play() override;

    /**
     * @brief   Play note.
     * @note    Commands are queued, so you can call this method without delay.
     * @param   note: Note_t to play;
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool play(Note note) override;

    /**
     * @brief   Play melody.
     * @param   melody: array of notes.
     * @param   len: length of the array. (Max len is skNoteQueueSize)
     * @retval  Execution status. 'true' - success, 'false' - otherwise.
     */
    virtual bool play(const Note melody[], uint8_t len) override;

    /**
     * @brief   Check whether the buzzer is playing.
     * @retval  'true' if playing, 'false' otherwise.
     */
    virtual bool isPlaying() override;

    /**
     * @brief   Stop playing melody.
     */
    virtual void stop() override;

private:

    // The pitch of a note that does not set one.
    static constexpr float skDefaultFrequencyHz = 4000.0f;

    // Hardware components
    Interface::IPwm *mPwm = nullptr;
    Interface::IGpo *mEn1 = nullptr;
    Interface::IGpo *mEn2 = nullptr;

    // Init flag
    bool mIsInit = false;

    // Enable flag
    bool mIsEnabled = false;

    // Buzzer is active
    bool mIsOn = false;

    /**
     * @brief   Constructor.
     */
    Buzzer() = default;

    /**
     * @brief   Destructor.
     */
    virtual ~Buzzer() = default;

    // RTOS components
    OS::Mutex mMutex;
    OS::Queue<Note, Interface::IBuzzer::skMaxNotes> mQueue;
    OS::Timer<Buzzer> mTimer;

    /**
     * @brief   Timer callback.
     */
    void nextNote(void *arg);

    /**
     * @brief   Stop playing the melody.
     */
    void stopPrv();
    /**
     * @brief   Set sound level.
     * @param   level: sound level 1,2,3, 0 - no sound
     */
    void setLevel(uint8_t level);

    /**
     * @brief   Set the pitch.
     * @param   frequency: in Hz, 0 - the default. A frequency the PWM rejects
     *          also plays the default.
     */
    void setFrequency(uint16_t frequency);

    /**
     * @brief   Set PWM.
     * @param   dutyPct: duty cycle in presents 0 - 100.0
     */
    void setPwm(uint8_t duty);

};

#endif /* __COMPONENT_BUZZER_HPP */
