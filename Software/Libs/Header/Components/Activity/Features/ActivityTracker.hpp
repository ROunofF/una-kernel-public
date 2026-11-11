/**
 ******************************************************************************
 * @file    ActivityTracker.hpp
 * @brief   Accumulator of active minutes derived from cadence + HR
 *          evidence + motion gating.
 *
 *          Two scoring paths, evaluated each tick() and integrated
 *          over time. Both credit at the full rate (1.0): a qualifying
 *          minute is a minute of activity whichever path proved it, so
 *          the running total reads as real elapsed active time.
 *           - Cadence path: stepsLastMinute >= 100 spm. Tudor-Locke
 *             moderate-intensity threshold; reliable evidence of
 *             walking-class activity. HR-independent.
 *           - HR + motion path: user is in motion AND a sliding window
 *             of recent HR samples shows sustained elevation (>= 2 of
 *             last 3 readings >= the moderate-intensity cutoff). Catches
 *             non-walking exertion (gym sets, stairs, cycling) where
 *             cadence is zero but heart rate confirms effort. The cutoff
 *             is the user's configured HR zone-1 upper bound (~50% HRmax,
 *             the WHO lower bound of moderate activity), fed in via
 *             setHrThresholdBpm(); it defaults to skDefaultHrThresholdBpm
 *             until settings are applied.
 *
 *          The two paths are OR'd, the higher score wins. Score is
 *          0..1, multiplied by inter-tick delta to credit milliseconds.
 *          Conservative integration: the lower of the two endpoint
 *          scores is used, so transitions don't over-credit.
 *
 *          HR evidence is fed via onHrSample(), rate-limited to one
 *          push per ~2 minutes so continuous-HR mode in ACTIVE does
 *          not flood the small evidence window. Untrusted samples
 *          (trust level < 1 or bpm == 0) are dropped.
 *
 *          The window is cleared by reset() and additionally goes
 *          stale (cleared) if no fresh sample arrives for over 5
 *          minutes - prevents old samples from lingering across
 *          wear-off / wear-on cycles.
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_FEATURE_ACTIVITY_TRACKER_HPP
#define COMPONENT_ACTIVITY_FEATURE_ACTIVITY_TRACKER_HPP

#include <cstdint>

namespace Activity
{

class ActivityTracker
{
public:
    /// Integrate the time since the previous tick and return the
    /// current cumulative active minutes. @p isUserMoving is true if
    /// either steps have been seen in the last minute or motion has
    /// been continuous for >= 30 s - i.e. the user is doing something
    /// (not just passively vibrating in a vehicle).
    uint32_t tick(uint32_t nowMs, bool isUserMoving, uint32_t stepsLastMin);

    /// Feed a HR sample into the evidence window. Called per parsed
    /// PPG frame from FeatureCenter::onHrSample. Rate-limited and
    /// trust-filtered internally.
    void onHrSample(uint8_t bpm, uint8_t trustLevel, uint32_t tsMs);

    /// Reset the accumulator (e.g. on an externally driven midnight
    /// rollover) and clear the HR evidence window.
    void reset(uint32_t baselineMinutes, uint32_t tsMs);

    /// Set the heart-rate cutoff (bpm) at/above which a sample counts as
    /// "elevated" for the HR path. Fed from the user's configured HR
    /// zones (zone-1 upper bound = ~50% HRmax). A zero value is ignored
    /// so a missing / uninitialised setting cannot drop the cutoff to 0
    /// and make every sample qualify.
    void setHrThresholdBpm(uint8_t bpm);

private:
    // Cadence path
    static constexpr uint32_t skCadenceThresholdSpm = 100;
    static constexpr float    skScoreCadence        = 1.0f;

    // HR + motion path
    // Default cutoff used until Backend feeds the user-configured value
    // via setHrThresholdBpm(). Mirrors the default HR zone-1 upper bound
    // (HeartRateZones::thresholds[0] = 95 bpm, ~50% HRmax = the WHO lower
    // bound of moderate intensity).
    static constexpr uint8_t  skDefaultHrThresholdBpm = 95;
    static constexpr uint8_t  skHrMinTrustLevel       = 1;
    static constexpr uint32_t skHrSampleMinIntervalMs = 120u * 1000u; ///< 2 min rate-limit
    static constexpr uint32_t skHrWindowStaleMs       = 5u * 60u * 1000u; ///< clear evidence if idle this long
    static constexpr uint8_t  skHrWindowSize          = 3;
    static constexpr uint8_t  skHrQuorum              = 2;
    static constexpr float    skScoreHrMotion         = 1.0f;

    // Integration safety.
    // Maximum inter-tick gap that may be credited as active time. Ticks are
    // driven by the Activity FSM's 30 s periodic timer (Service::skFsmTickMs)
    // plus motion/touch rising edges, so a legitimate gap is ~30 s. A gap
    // several times larger means ticks were not actually delivered across it
    // (thread stall, a deep-sleep state that dropped the FSM timer as a wake
    // source, or a tick-restore glitch) - the sensors were not observing, so
    // crediting the whole gap would invent active minutes. Cap at 3x the tick
    // period to absorb scheduling jitter while bounding an unobserved gap.
    static constexpr uint32_t skMaxCreditableGapMs    = 90u * 1000u;

    float computeScore(uint32_t stepsLastMin, bool isUserMoving) const;
    bool  hrPathQualified() const;
    void  expireStaleHrWindow(uint32_t nowMs);
    void  pushHrSample(uint8_t bpm);

    // Integrator state
    bool     mSeeded          = false;
    uint32_t mLastTickMs      = 0;
    float    mLastScore       = 0.0f;
    uint32_t mAccumulatedMs   = 0;
    uint32_t mBaselineMinutes = 0;

    // HR evidence window (small fixed array; 3 slots, ring overwrite)
    uint8_t  mHrSamples[skHrWindowSize] = {0, 0, 0};
    uint8_t  mHrSampleCount             = 0;
    uint8_t  mHrSampleIdx               = 0;
    uint32_t mLastHrSampleMs            = 0;

    // Moderate-intensity HR cutoff (bpm). Runtime-configurable from the
    // user's HR zones; see setHrThresholdBpm().
    uint8_t  mHrThresholdBpm            = skDefaultHrThresholdBpm;
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_FEATURE_ACTIVITY_TRACKER_HPP
