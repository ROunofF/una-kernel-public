/**
 ******************************************************************************
 * @file    Debounce.cpp
 * @brief   Debounce filter class.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Debounce.hpp"

#define LOG_MODULE_PRX      "Debounce"
#define LOG_MODULE_LEVEL    LOG_LEVEL_ERROR
#include "Logger.h"

#include "DebounceDaemon.hpp"
#include "System.hpp"

namespace Filter
{

Debounce::Debounce()
{
    DebounceDaemon::GetInstance().addObj(this);
}

Debounce::Debounce(uint32_t timeOn, uint32_t timeOff, bool outState)
{
    DebounceDaemon::GetInstance().addObj(this);

    configTimings(timeOn, timeOff);
    setOut(outState);
}

Debounce::~Debounce()
{
    DebounceDaemon::GetInstance().delObj(this);
}

void Debounce::configTimings(uint32_t timeOn, uint32_t timeOff)
{
    mTimeOn = timeOn;
    mTimeOff = timeOff;
}

void Debounce::setOut(bool value)
{
    mOutput = value;
    mInput = value;
    mIsActive = false;
}

void Debounce::add(bool value)
{
    if (mInput == value) {
        return;
    }

    mInput = value;
    mTimeToUpd = Driver::System::GetTimeMs() + (mInput ? mTimeOn : mTimeOff);
    mIsActive = true;

    DebounceDaemon::GetInstance().notify();
}

bool Debounce::get()
{
    return mOutput;
}

void Debounce::onOutChange(bool out)
{
    if (mpOutHandler != nullptr) {
        mpOutHandler->outHandler(this, out);
    }
}

bool Debounce::execute()
{
    if (!mIsActive) {
        return false;
    }

    if (Driver::System::GetTimeMs() >= mTimeToUpd) {
        if (mOutput != mInput) {
            mOutput = mInput;
            onOutChange(mOutput);
        }
        mIsActive = false;
    }

    return mIsActive;
}

} /* namespace Filter */
