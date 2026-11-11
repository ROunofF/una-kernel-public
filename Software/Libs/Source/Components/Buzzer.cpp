/**
 ******************************************************************************
 * @file    Buzzer.cpp
 * @brief   Buzzer Driver.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Buzzer.hpp"

#define LOG_MODULE_PRX      "Buzzer"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>
#include "main.h"

#if (USE_SYSTEM_SLEEP == 1)
#include "Sleep.hpp"
static void Pause()
{
    Driver::Sleep::Pause();
}

static void Resume()
{
    Driver::Sleep::Resume();
}
#else
static void Pause(){}
static void Resume(){}
#endif // (USE_SYSTEM_SLEEP == 1)

#include "Gpio.hpp"
#include "Pwm.hpp"

bool Buzzer::init()
{
    assert(mEn1 != nullptr);
    assert(mEn2 != nullptr);
    assert(mPwm != nullptr);

    if (mIsInit) {
        return true;
    }

    mMutex.init(LOG_MODULE_PRX, true);

    if (mMutex.lock()) {
        mEn1->init(Interface::IGpo::Type::PP, false);
        mEn2->init(Interface::IGpo::Type::PP, false);
        mPwm->init();
        // The piezo resonates far higher (about 8 kHz in the case), so the
        // default sets how loud every unpitched alert is, not just its pitch.
        mPwm->setFrequency(skDefaultFrequencyHz);

        mQueue.init(LOG_MODULE_PRX);
        mTimer.init(LOG_MODULE_PRX, this, &Buzzer::nextNote);

        mIsInit = true;

        LOG_INFO("Initialized\n");
        mMutex.release();
    }

    return mIsInit;
}

bool Buzzer::deinit()
{
    if (!mIsInit) {
        return true;
    }

    if (mMutex.lock()) {

        disable();

        // It makes no sense to de-initialize RTOS components
        // in a static object
        // De-initialize hardware recourses only
        mPwm->deinit();
        mEn2->deinit();
        mEn1->deinit();

        mIsInit = false;

        LOG_INFO("Deinitialized\n");
        mMutex.release();
    }

    return !mIsInit;
}

bool Buzzer::enable()
{
    if (!mIsInit) {
        return false;
    }

    if (mMutex.lock()) {
        mIsEnabled = true;
        LOG_INFO("Enabled\n");
        mMutex.release();
    }
    return true;
}

bool Buzzer::disable()
{
    if (!mIsInit) {
        return false;
    }

    if (mMutex.lock()) {
        stop();
        mIsEnabled = false;
        LOG_INFO("Disabled\n");
        mMutex.release();
    }
    return true;
}

bool Buzzer::play()
{
    Note note {};
    return play(note);
}

bool Buzzer::play(Note note)
{
    return play(&note, 1);
}

bool Buzzer::play(const Note melody[], uint8_t len)
{
    if (!mIsEnabled) {
        return false;
    }

    for (uint8_t i = 0; i < len; i++) {
        if (!mQueue.push(melody[i], 100)) {
            LOG_ERROR("Failed to put note in queue\n");
            return false;
        }
    }

    if (mMutex.lock()) {
        if (mIsOn == false) {
            mIsOn = true;
            Pause();
            LOG_DEBUG("Start\n");
        }
        mMutex.release();
    }

    nextNote(nullptr);

    return true;
}

bool Buzzer::isPlaying()
{
    return mIsOn;
}

void Buzzer::stop()
{
    if (!mIsEnabled) {
        return;
    }

    if (mMutex.lock()) {
        mTimer.stop();
        mQueue.clear();
        stopPrv();
        mMutex.release();
    }
}

void Buzzer::nextNote(void *arg)
{
    Note note {};
    if (mMutex.lock()) {

        if (mQueue.size() > 0 && mQueue.pop(note, 1)) {
            LOG_DEBUG("Note: time %u, level %u, %u Hz\n", note.time, note.level, note.frequency);
            setFrequency(note.frequency);
            setLevel(note.level);
            setPwm(50);
            mTimer.start(note.time);
        } else {
            LOG_DEBUG("End\n");
            stopPrv();
        }

        mMutex.release();
    }

}

void Buzzer::stopPrv()
{
    if (mIsOn == true) {
        mIsOn = false;
        setPwm(0);
        setLevel(0);
        Resume();
        LOG_DEBUG("Stop\n");
    }
}

void Buzzer::setFrequency(uint16_t frequency)
{
    const float hz = (frequency != 0) ? static_cast<float>(frequency) : skDefaultFrequencyHz;

    // Each change restarts the timer, so a run of notes at one pitch is left
    // alone.
    if (hz == mPwm->getFrequency()) {
        return;
    }
    if (!mPwm->setFrequency(hz)) {
        mPwm->setFrequency(skDefaultFrequencyHz);
    }
}

void Buzzer::setLevel(uint8_t level)
{
    switch (level) {
        case 1:
            mEn1->set(false);
            mEn2->set(true);
            break;

        case 2:
            mEn1->set(true);
            mEn2->set(false);
            break;

        case 3:
            mEn1->set(true);
            mEn2->set(true);
            break;

        default:
            mEn1->set(false);
            mEn2->set(false);
            break;
    }
}

void Buzzer::setPwm(uint8_t duty)
{
    static bool sTimOn;
    mPwm->setDuty(duty, 0);
    if (duty > 0) {
        if (!sTimOn) {
            mPwm->start(true);
            sTimOn = true;
        }
    } else {
        if (sTimOn) {
            mPwm->stop();
            sTimOn = false;
        }
    }
}

