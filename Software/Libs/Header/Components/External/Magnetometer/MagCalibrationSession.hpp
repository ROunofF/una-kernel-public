/**
 ******************************************************************************
 * @file    MagCalibrationSession.hpp
 * @brief   The arithmetic of one calibration or verification run.
 *
 *          Fed raw samples, it fits a correction (FIT) or measures the one
 *          under test (CHECK). No clock, lock or callbacks.
 *
 * @note    Threading: start*() and finish() are called by the owner with no
 *          feed() in progress or able to begin; feed() is called by one
 *          thread at a time between them. getProgress() may be called from
 *          any thread at any time.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_CALIBRATION_SESSION_HPP
#define COMPONENT_MAG_CALIBRATION_SESSION_HPP

#include <atomic>
#include <cstdint>

#include "MagCalibration.hpp"
#include "MagCalibrator.hpp"

class MagCalibrationSession
{

public:

    enum class Mode : uint8_t {
        FIT = 0,    ///< Fit a correction to the samples.
        CHECK,      ///< Measure the correction under test on the samples.
    };

    struct Result {
        Mode mode = Mode::FIT;

        /// 'false' when the run was ended early; nothing below is filled then.
        bool completed = false;

        // FIT
        MagCalibration::Verdict verdict = MagCalibration::Verdict::TOO_FEW_SAMPLES;
        MagCalibration::Record  record;         ///< Default when nothing was fitted.
        MagCalibrator::Progress progress;       ///< Samples and spans fitted from.
        uint8_t                 coverage = 0;   ///< Of MagCalibrator::skDirections.
        bool                    haveSphere    = false;
        MagSphereFit::Result    sphere;
        bool                    haveEllipsoid = false;
        MagEllipsoidFit::Result ellipsoid;

        // CHECK
        bool                        haveCheck = false;
        bool                        passed    = false;
        MagCalibrationCheck::Result check;

        /// A completed fit good enough to keep: a poor fit is worse than no
        /// correction at all.
        bool fitAccepted() const
        {
            return completed && (mode == Mode::FIT) &&
                   (verdict == MagCalibration::Verdict::OK);
        }
    };

    MagCalibrationSession() = default;

    MagCalibrationSession(const MagCalibrationSession&)            = delete;
    MagCalibrationSession& operator=(const MagCalibrationSession&) = delete;

    /// Begin fitting; discards whatever was fed before. expectedSamples is as
    /// for MagCalibrator::start().
    void startFit(uint32_t expectedSamples);

    /// Begin checking a copy of record; discards whatever was fed before.
    /// 'false', and nothing started, if the record cannot be applied.
    bool startCheck(const MagCalibration::Record& record);

    /// One raw sample, as the sensor reported it.
    void feed(float x, float y, float z);

    /// Lock-free; read during feed(), the spans may mix two samples.
    MagCalibrator::Progress getProgress() const;

    /// End the run; nothing is concluded unless completed. The session may be
    /// started again afterwards.
    Result finish(bool completed);

private:

    void publishProgress(const MagCalibrator::Progress& progress);

    static uint32_t toBits(float value);
    static float fromBits(uint32_t bits);

    Mode mMode = Mode::FIT;

    MagCalibrator       mCalibrator;
    MagCalibrationCheck mCheck;

    std::atomic<uint32_t> mSamples {0};
    std::atomic<uint32_t> mSpanX   {0};   ///< float bits
    std::atomic<uint32_t> mSpanY   {0};
    std::atomic<uint32_t> mSpanZ   {0};

    /// Fewer samples than this fail a check.
    static constexpr uint32_t skMinCheckSamples = 150;

};

#endif // COMPONENT_MAG_CALIBRATION_SESSION_HPP
