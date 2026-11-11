/**
 ******************************************************************************
 * @file    MagCorrection.cpp
 * @brief   The magnetic correction in force, and the keeping of it.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "MagCorrection"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Magnetometer/MagCorrection.hpp"

MagCorrection::MagCorrection(Interface::IMagCalibrationStore& store)
    : mStore(store)
{
    mGuarded = mMutex.init(LOG_MODULE_PRX);

    if (!mGuarded) {
        // Unserialised, a sample could be corrected with a half-replaced
        // record. Creation fails deterministically, the same on every boot,
        // so go inert rather than degrade.
        LOG_ERROR("Lock not created; the correction is inert\n");

        return;
    }

    OS::MutexCS lock(mMutex);

    MagCalibration::Record stored;

    if (mStore.load(stored)) {
        // The store vouches for the bytes; set() for the values.
        (void) mCalibration.set(stored);
    }
}

bool MagCorrection::setCalibration(const MagCalibration::Record& record)
{
    if (!mGuarded) {
        return false;
    }

    // Held across both halves, so two writers cannot leave the store holding
    // a different record from the one in force.
    OS::MutexCS lock(mMutex);

    if (!mCalibration.set(record)) {
        return false;
    }

    return mStore.save(record);
}

bool MagCorrection::getCalibration(MagCalibration::Record& record) const
{
    if (!mGuarded) {
        return false;
    }

    OS::MutexCS lock(mMutex);

    if (!mCalibration.isValid()) {
        return false;
    }

    record = mCalibration.get();

    return true;
}

bool MagCorrection::clearCalibration()
{
    if (!mGuarded) {
        return false;
    }

    OS::MutexCS lock(mMutex);

    mCalibration.clear();

    return mStore.clear();
}

bool MagCorrection::apply(float& x, float& y, float& z) const
{
    if (!mGuarded) {
        return false;
    }

    OS::MutexCS lock(mMutex);

    // Answered from the record that did the correcting, under the same lock.
    return mCalibration.apply(x, y, z);
}

bool MagCorrection::isCalibrated() const
{
    if (!mGuarded) {
        return false;
    }

    OS::MutexCS lock(mMutex);

    return mCalibration.isValid();
}
