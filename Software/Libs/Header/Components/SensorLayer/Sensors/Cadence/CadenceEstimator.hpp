/**
 ******************************************************************************
 * @file    CadenceEstimator.hpp
 * @brief   Windowed step-counter running cadence estimator.
 *
 * Cadence is the cumulative STEP_COUNTER delta across a sliding window of
 * retained samples, divided by the actual timestamp span of those samples.
 * The cumulative count is robust to the ms-quantised/batched per-step timing
 * that made inter-step-interval estimation spuriously halve and re-acquire
 * at high cadence.
 *
 * Emits cadence only. Implied step length is derived SDK-side at FIT
 * record-write time (see SDK::Calibration::StrideMath), not in the kernel.
 ******************************************************************************
 */

#ifndef CADENCE_ESTIMATOR_HPP
#define CADENCE_ESTIMATOR_HPP

#include "SensorLayer/Sensors/Cadence/CadenceEstimatorConfig.hpp"

#include <cstdint>

namespace Sensor
{

class CadenceEstimator {
public:
    struct Sample {
        float cadenceSpm     = 0.0f;
        bool  cadenceValid   = false;
    };

    CadenceEstimator();

    void reset();
    void onStepCount(uint64_t timestampUs, uint32_t cumulativeSteps);
    void tickUs(uint64_t nowUs);
    Sample getSample() const;

private:
    struct CounterSample {
        uint64_t tUs;
        uint32_t count;
    };

    CounterSample&       newestEntry();
    const CounterSample& newestEntry() const;
    const CounterSample& oldestEntry() const;
    void pushEntry(uint64_t tUs, uint32_t count);
    void evictOutsideWindow(uint64_t nowUs);
    void invalidate();

    CounterSample mRing[CadenceConfig::kMaxSamples] = {};
    uint32_t      mHead = 0;    ///< index of the oldest retained sample
    uint32_t      mSize = 0;

    float mCadenceSpm       = 0.0f;
    bool  mCadenceValid     = false;
};

} // namespace Sensor

#endif // CADENCE_ESTIMATOR_HPP
