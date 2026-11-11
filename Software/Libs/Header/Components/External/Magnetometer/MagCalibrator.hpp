/**
 ******************************************************************************
 * @file    MagCalibrator.hpp
 * @brief   The two halves of a magnetometer calibration run.
 *
 *          MagCalibrator fits an ellipsoid and a sphere to a tumble and
 *          returns the ellipsoid's record with a verdict. MagCalibrationCheck
 *          judges a record on samples it was not fitted to, which an
 *          in-sample residual cannot do. Neither touches hardware.
 ******************************************************************************
 */

#ifndef COMPONENT_MAG_CALIBRATOR_HPP
#define COMPONENT_MAG_CALIBRATOR_HPP

#include "MagCalibration.hpp"
#include "MagEllipsoidFit.hpp"
#include "MagSphereFit.hpp"

#include <cstdint>

class MagCalibrator
{

public:

    struct Progress {
        uint32_t samples = 0;

        /// Per-axis extent; about twice the field magnitude once covered.
        float spanX = 0.0f;
        float spanY = 0.0f;
        float spanZ = 0.0f;
    };

    /// Faces, edges and corners of a cube centred on the field.
    static constexpr uint8_t skDirections = 26;

    MagCalibrator();

    void setLimits(const MagCalibration::Limits& limits);
    const MagCalibration::Limits& getLimits() const;

    /**
     * @brief   Discard whatever was collected and begin a run.
     * @param   expectedSamples: How many samples the run is expected to feed.
     *          The kept samples are spread over that many, so the coverage a
     *          tumble reaches does not depend on where the run happens to end.
     *          0 when the length is not known: they are then thinned as the
     *          run grows.
     */
    void start(uint32_t expectedSamples);

    /// Fold one raw, uncorrected sample into both fits.
    void add(float x, float y, float z);

    Progress getProgress() const;

    /**
     * @brief   Fit, judge, and produce the ellipsoid's record.
     * @param   record: Filled in whenever both fits were placed, even when the
     *          verdict rejects it.
     * @retval  Verdict::OK when the record may be used.
     */
    MagCalibration::Verdict finish(MagCalibration::Record& record);

    /// Directions reached, seen from the sphere's centre; zero until finish().
    /// Samples within half of Limits::minRadius of that centre do not count.
    uint8_t getCoverage() const;

    /// 'true', with result filled in, if an ellipsoid was fitted this run.
    bool getEllipsoid(MagEllipsoidFit::Result& result) const;

    /// 'true', with result filled in, if finish() could place a sphere.
    bool getSphere(MagSphereFit::Result& result) const;

private:

    /// Kept to measure coverage, in fixed memory. Thinned by half only when a
    /// run feeds more than it was expected to.
    static constexpr uint16_t skKeptSamples = 256;

    /// Kept over a run of the expected length: three quarters of the buffer,
    /// so a third more samples than expected still fit. The coverage limit
    /// holds for about this many.
    static constexpr uint16_t skKeptTarget = 192;

    /// Kept samples a direction needs to count, so one stray does not.
    static constexpr uint8_t skMinPerDirection = 2;

    uint8_t countCoveredDirections() const;

    MagSphereFit           mSphere;
    MagEllipsoidFit        mEllipsoid;
    MagCalibration::Limits mLimits;

    bool                    mHaveSphere;
    bool                    mHaveEllipsoid;
    MagSphereFit::Result    mSphereResult;
    MagEllipsoidFit::Result mEllipsoidResult;

    float    mKept[skKeptSamples][3];
    uint16_t mKeptCount;
    /// A sample is kept each time the credit reaches mKeepOver, which spreads
    /// skKeptTarget samples evenly over mKeepOver fed.
    uint32_t mKeepOver;
    uint32_t mKeepCredit;
    uint8_t  mCoverage;

};

/**
 * @brief   Measures a calibration against samples it was not fitted to.
 */
class MagCalibrationCheck
{

public:

    struct Result {
        uint32_t samples = 0;

        float mean = 0.0f;      ///< Mean corrected magnitude.
        float spread = 0.0f;    ///< RMS deviation from that mean.
        float minimum = 0.0f;
        float maximum = 0.0f;
    };

    MagCalibrationCheck();

    /// Begin checking record, discarding what was fed before. 'false', and
    /// nothing changed, if the record cannot be applied.
    bool start(const MagCalibration::Record& record);

    /// Fold one raw sample in; it is corrected here.
    void add(float x, float y, float z);

    uint32_t getCount() const;

    bool getResult(Result& result) const;

    /**
     * @brief   Whether the correction held: both the scatter of the corrected
     *          magnitude and its mean's distance from the calibration's radius
     *          within maxCorrectedSpreadFactor. Fewer than minSamples fails.
     */
    bool passed(const MagCalibration::Limits& limits,
                uint32_t minSamples) const;

private:

    /// The correction under test; invalid until a start is taken.
    MagCalibration mCalibration;

    uint32_t mCount;
    double   mSum;
    double   mSumSq;
    float    mMin;
    float    mMax;

};

#endif // COMPONENT_MAG_CALIBRATOR_HPP
