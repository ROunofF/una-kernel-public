/**
 ******************************************************************************
 * @file    MagCorrection.hpp
 * @brief   The magnetic correction in force, and the keeping of it.
 *
 *          Owns the correction in force and, through a store nothing else may
 *          reach, the copy that survives a restart. One lock serialises both,
 *          so apply() may wait on a store write.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_CORRECTION_HPP
#define COMPONENT_MAG_CORRECTION_HPP

#include "IMagCorrection.hpp"
#include "IMagCalibrationStore.hpp"
#include "MagCalibration.hpp"
#include "OS.hpp"

class MagCorrection : public Interface::IMagCorrection
{

public:

    /**
     * @brief   Take up whatever the store holds, if anything.
     * @note    If the lock cannot be created the object is inert: it corrects
     *          nothing and reports no calibration. Construct after the
     *          scheduler starts.
     * @param   store: Not owned; must outlive this.
     */
    explicit MagCorrection(Interface::IMagCalibrationStore& store);

    MagCorrection(const MagCorrection&)            = delete;
    MagCorrection& operator=(const MagCorrection&) = delete;

    /**
     * @brief   Apply a correction, then keep it; one that cannot be applied is
     *          not kept.
     * @retval  'true' if applied and kept. 'false' after applying means a
     *          restart comes up without it.
     */
    bool setCalibration(const MagCalibration::Record& record);

    /// 'true', with record filled in, if a correction is being applied.
    bool getCalibration(MagCalibration::Record& record) const;

    /**
     * @brief   Stop correcting and erase the kept correction.
     * @retval  'false' if the erase failed: a restart brings it back.
     */
    bool clearCalibration();

    //// IMagCorrection
    bool apply(float& x, float& y, float& z) const override;
    bool isCalibrated() const override;

private:

    /// Non-recursive: a public method takes it once, helpers never.
    mutable OS::Mutex mMutex;

    MagCalibration    mCalibration;

    /// Not owned. Reached only from under the lock.
    Interface::IMagCalibrationStore& mStore;

    /// Written once in the constructor; false makes every method a no-op.
    bool mGuarded = false;

};

#endif // COMPONENT_MAG_CORRECTION_HPP
