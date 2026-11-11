/**
 ******************************************************************************
 * @file    HrSourceArbiter.hpp
 * @brief   Selects the live heart-rate source (optical vs external strap).
 ******************************************************************************
 *
 * Sits between the HR producers and BodySubSensorHeartRate. The optical sensor
 * and an external accessory (ProfileHrs via AccessoryCentral) both submit BPM
 * samples; the arbiter picks which one feeds the sensor layer:
 *
 *   - prefer the external strap while its samples are fresh and valid;
 *     otherwise fall back to optical;
 *   - a hysteresis window damps flapping when both are present — but a source
 *     that goes stale is abandoned immediately (you can't hold a dead source),
 *     so a strap dropout falls back to optical within the freshness window.
 *
 * Trust is forwarded from the selected sample (ProfileHrs already maps sensor
 * contact to the downstream trust scale; optical trust passes through). The
 * arbiter is pure — it takes the current time as a parameter and holds no
 * BlueNRG/OS state — so the policy is exercised by host unit tests.
 *
 ******************************************************************************
 */

#ifndef __SENSOR_HR_SOURCE_ARBITER_HPP
#define __SENSOR_HR_SOURCE_ARBITER_HPP

#include <cstdint>

namespace Sensor
{

/**
 * @brief   Which producer a heart-rate sample came from.
 */
enum class HrSource : uint8_t {
    None     = 0,
    Optical  = 1,
    External = 2,
};

/**
 * @brief   Arbitrates the live heart-rate source.
 */
class HrSourceArbiter {
public:

    // A sample is "fresh" if seen within this window; the selected source is
    // abandoned once its last sample ages past it.
    static constexpr uint32_t kFreshMs = 5000;
    // Minimum dwell before switching *between* two simultaneously-available
    // sources (anti-flap). Does not delay abandoning a stale source.
    static constexpr uint32_t kDebounceMs = 5000;

    struct Output {
        bool     valid  { false };           // a source is selected
        uint8_t  bpm    { 0 };
        uint8_t  trust  { 0 };
        HrSource source { HrSource::None };
    };

    // A single source's current reading (for the multi-source HEART_RATE_EX
    // frame): the raw value if fresh, else invalid/zero.
    struct Reading {
        bool    valid { false };
        uint8_t bpm   { 0 };
        uint8_t trust { 0 };
    };

    // The arbitrated decision plus the raw per-source readings, so the sensor
    // layer can publish internal (PPG), external (strap), and arbitrated HR.
    struct ExOutput {
        Output  arbitrated {};
        Reading optical {};
        Reading external {};
    };

    /**
     * @brief   Submit an optical reading.
     * @param   bpm/trust: reading and its trust level.
     * @param   nowMs: current monotonic time (ms).
     */
    void submitOptical(uint8_t bpm, uint8_t trust, uint32_t nowMs);

    /**
     * @brief   Submit an external (strap) reading.
     */
    void submitExternal(uint8_t bpm, uint8_t trust, uint32_t nowMs);

    /**
     * @brief   External-only mode: when set, the optical sensor is never
     *          selected — `decide()` returns the fresh external reading or, if
     *          there is none, an invalid output (valid == false). Cleared back
     *          to the default external-with-wrist-fallback policy on release.
     */
    void setExternalOnly(bool on) { mExternalOnly = on; }

    /**
     * @brief   Re-evaluate the selection at nowMs and return the chosen output.
     *          Applies freshness + hysteresis and updates the selected source.
     */
    Output decide(uint32_t nowMs);

    /**
     * @brief   Like decide(), but also reports the raw optical and external
     *          readings (fresh-or-invalid) alongside the arbitrated output, for
     *          the multi-source HEART_RATE_EX frame.
     */
    ExOutput decideEx(uint32_t nowMs);

    /**
     * @brief   Currently selected source.
     */
    HrSource selectedSource() const { return mSelected; }

    /**
     * @brief   Forget all samples and selection (e.g. on workout boundary).
     */
    void reset();

    /**
     * @brief   Forget the last external (strap) reading and drop External as the
     *          selected source. Used on accessory release so the arbiter cannot
     *          keep preferring a stale strap sample for up to the freshness
     *          window after the strap is gone. Optical state is untouched.
     */
    void clearExternal();

private:

    struct Sample {
        bool     present { false };
        uint8_t  bpm     { 0 };
        uint8_t  trust   { 0 };
        uint32_t ts      { 0 };
    };

    Sample   mOptical {};
    Sample   mExternal {};
    HrSource mSelected { HrSource::None };
    uint32_t mLastSwitchMs { 0 };
    bool     mExternalOnly { false };   // optical disallowed (external-only mode)

    static bool fresh(const Sample &s, uint32_t nowMs)
    {
        return s.present && (nowMs - s.ts) <= kFreshMs;
    }
};

} /* namespace Sensor */

#endif /* __SENSOR_HR_SOURCE_ARBITER_HPP */
