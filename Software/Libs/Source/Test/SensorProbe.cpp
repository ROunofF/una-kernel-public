/**
 ******************************************************************************
 * @file    SensorProbe.cpp
 * @brief   The checks the on-device SensorLayer tests apply to a probe
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "SensorProbe"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/SensorProbe.hpp"

void SensorProbeTest::measure(Probe& probe, const Expect& expect, uint32_t forMs, bool newRun)
{
    OS::Delay(1500);

    if (newRun) {
        probe.restartAll(expect.gapMaxMs, expect.oldestAvgMaxMs);
    } else {
        probe.restart(expect.gapMaxMs, expect.oldestAvgMaxMs);
    }

    OS::Delay(forMs);
}

void SensorProbeTest::check(const char* name, Probe& probe, const Expect& expect)
{
    const Probe::Snapshot s = probe.get();

    const uint32_t avgGap     = (s.gaps != 0) ? s.sumGapMs / s.gaps : 0;
    const uint32_t avgCount10 = (s.deliveries != 0) ? (s.samples * 10) / s.deliveries : 0;
    const uint32_t avgOldest  = (s.deliveries != 0) ? s.sumOldestMs / s.deliveries : 0;

    const bool cadenceOk = (s.gaps != 0)
                        && (avgGap >= expect.gapMinMs) && (avgGap <= expect.gapMaxMs)
                        && (avgCount10 >= expect.samplesMin10) && (avgCount10 <= expect.samplesMax10);
    const bool ageOk     = (s.deliveries != 0) && (avgOldest <= expect.oldestAvgMaxMs);
    const bool stampsOk  = (s.zeroStamps == 0) && (s.duplicates == 0) && (s.backwards == 0);

    const bool ok = cadenceOk && ageOk && stampsOk;

    LOG_INFO("[%s] %s: deliveries %lu, gap %lu..%lu avg %lu ms, late %lu, samples %lu..%lu avg %lu.%lu\n",
             name,
             probe.name(),
             static_cast<unsigned long>(s.deliveries),
             static_cast<unsigned long>(s.minGapMs),
             static_cast<unsigned long>(s.maxGapMs),
             static_cast<unsigned long>(avgGap),
             static_cast<unsigned long>(s.lateGaps),
             static_cast<unsigned long>(s.minSamples),
             static_cast<unsigned long>(s.maxSamples),
             static_cast<unsigned long>(avgCount10 / 10),
             static_cast<unsigned long>(avgCount10 % 10));
    LOG_INFO("[%s] oldest avg %lu max %ld of %lu ms, stale %lu, newest %ld..%ld ms, step %lu..%lu us, "
             "zero %lu, twice %lu, back %lu -> %s\n",
             name,
             static_cast<unsigned long>(avgOldest),
             static_cast<long>(s.maxOldestMs),
             static_cast<unsigned long>(expect.oldestAvgMaxMs),
             static_cast<unsigned long>(s.stale),
             static_cast<long>(s.minNewestMs),
             static_cast<long>(s.maxNewestMs),
             static_cast<unsigned long>(s.minStepUs),
             static_cast<unsigned long>(s.maxStepUs),
             static_cast<unsigned long>(s.zeroStamps),
             static_cast<unsigned long>(s.duplicates),
             static_cast<unsigned long>(s.backwards),
             ok ? "PASS" : "FAIL");

    if (!ok) {
        ++mFailures;
    }

    Probe::Delivery trace[Probe::kTraceLength];
    const uint32_t  held = probe.trace(trace);

    for (uint32_t i = 0; i < held; ++i) {
        LOG_INFO("[%s]   at %lu (+%lu): %u samples, stamped %lu..%lu\n",
                 name,
                 static_cast<unsigned long>(trace[i].atMs),
                 static_cast<unsigned long>((i != 0) ? trace[i].atMs - trace[i - 1].atMs : 0),
                 static_cast<unsigned>(trace[i].count),
                 static_cast<unsigned long>(trace[i].firstMs),
                 static_cast<unsigned long>(trace[i].lastMs));
    }
}

void SensorProbeTest::checkRate(const char* name, Probe& probe, const Rate& expect)
{
    const Probe::Snapshot s = probe.get();
    const uint32_t elapsed  = s.lastMs - s.firstMs;
    const uint32_t perSec10 = (elapsed != 0) ? (s.laterSamples * 10000u) / elapsed : 0;

    const bool ok = (s.steps != 0)
                 && (perSec10 >= expect.perSecMin10) && (perSec10 <= expect.perSecMax10)
                 && (s.minStepUs >= expect.stepMinUs) && (s.maxStepUs <= expect.stepMaxUs);

    LOG_INFO("[%s] %lu.%lu samples a second, step %lu..%lu us -> %s\n",
             name,
             static_cast<unsigned long>(perSec10 / 10),
             static_cast<unsigned long>(perSec10 % 10),
             static_cast<unsigned long>(s.minStepUs),
             static_cast<unsigned long>(s.maxStepUs),
             ok ? "PASS" : "FAIL");

    if (!ok) {
        ++mFailures;
    }
}
