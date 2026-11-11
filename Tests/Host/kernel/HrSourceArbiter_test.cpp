/**
 * Host unit tests for Sensor::HrSourceArbiter — the optical vs external-strap
 * heart-rate source selection policy. Pure logic, no BlueNRG/OS deps.
 */

#include <cstdint>

#include <gtest/gtest.h>

#include "Components/SensorLayer/Sensors/Body/HrSourceArbiter.hpp"

namespace {

using Sensor::HrSource;
using Sensor::HrSourceArbiter;

constexpr uint32_t kFresh = HrSourceArbiter::kFreshMs;
constexpr uint32_t kDwell = HrSourceArbiter::kDebounceMs;

} // namespace

TEST(HrSourceArbiter, NoSamplesIsNone)
{
    HrSourceArbiter a;
    auto out = a.decide(0);
    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.source, HrSource::None);
}

TEST(HrSourceArbiter, OpticalSelectedImmediatelyAndForwarded)
{
    HrSourceArbiter a;
    a.submitOptical(72, 3, 1000);
    auto out = a.decide(1000);
    EXPECT_TRUE(out.valid);
    EXPECT_EQ(out.source, HrSource::Optical);
    EXPECT_EQ(out.bpm, 72);
    EXPECT_EQ(out.trust, 3);
}

TEST(HrSourceArbiter, OpticalGoesNoneWhenStale)
{
    HrSourceArbiter a;
    a.submitOptical(72, 3, 0);
    EXPECT_EQ(a.decide(0).source, HrSource::Optical);
    auto out = a.decide(kFresh + 1);
    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.source, HrSource::None);
}

TEST(HrSourceArbiter, ExternalNotPreferredUntilDwellElapses)
{
    HrSourceArbiter a;
    a.submitOptical(70, 3, 0);
    EXPECT_EQ(a.decide(0).source, HrSource::Optical);  // None->Optical at t=0

    // External now available, but both viable: hold optical through the dwell.
    a.submitExternal(80, 3, 100);
    a.submitOptical(71, 3, 100);
    EXPECT_EQ(a.decide(100).source, HrSource::Optical);

    // After the dwell, prefer external.
    a.submitExternal(82, 3, kDwell);
    a.submitOptical(71, 3, kDwell);
    auto out = a.decide(kDwell);
    EXPECT_EQ(out.source, HrSource::External);
    EXPECT_EQ(out.bpm, 82);
}

TEST(HrSourceArbiter, ExternalZeroBpmIsIgnored)
{
    HrSourceArbiter a;
    a.submitExternal(0, 3, 0);   // ProfileHrs never emits this, but be safe
    a.submitOptical(68, 2, 0);
    auto out = a.decide(0);
    EXPECT_EQ(out.source, HrSource::Optical);
}

TEST(HrSourceArbiter, StrapDropoutFallsBackToOpticalWithinFreshWindow)
{
    HrSourceArbiter a;
    // Drive selection to External.
    a.submitOptical(70, 3, 0);
    a.decide(0);
    a.submitExternal(80, 3, kDwell);
    a.submitOptical(70, 3, kDwell);
    ASSERT_EQ(a.decide(kDwell).source, HrSource::External);

    // Strap stops; optical keeps arriving. Once the external sample ages past
    // the fresh window the arbiter abandons it and reports optical again.
    uint32_t t = kDwell + 1000;
    a.submitOptical(75, 3, t);
    EXPECT_EQ(a.decide(t).source, HrSource::External);  // external still fresh

    t = kDwell + kFresh + 1;  // external (last @ kDwell) now stale
    a.submitOptical(76, 3, t);
    auto out = a.decide(t);
    EXPECT_EQ(out.source, HrSource::Optical);
    EXPECT_EQ(out.bpm, 76);
}

TEST(HrSourceArbiter, ExternalContactLostTrustForwarded)
{
    HrSourceArbiter a;
    // Get onto external (trust 3), then a contact-lost sample (trust 1).
    a.submitOptical(70, 3, 0);
    a.decide(0);
    a.submitExternal(80, 3, kDwell);
    a.submitOptical(70, 3, kDwell);
    ASSERT_EQ(a.decide(kDwell).source, HrSource::External);

    a.submitExternal(78, 1, kDwell + 500);  // contact lost -> degraded trust
    auto out = a.decide(kDwell + 500);
    EXPECT_EQ(out.source, HrSource::External);
    EXPECT_EQ(out.trust, 1);
}

TEST(HrSourceArbiter, BothStaleIsNone)
{
    HrSourceArbiter a;
    a.submitOptical(70, 3, 0);
    a.submitExternal(80, 3, 0);
    a.decide(0);
    auto out = a.decide(kFresh + 1);
    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.source, HrSource::None);
}

TEST(HrSourceArbiter, ResetClearsSelection)
{
    HrSourceArbiter a;
    a.submitOptical(70, 3, 0);
    a.decide(0);
    a.reset();
    EXPECT_EQ(a.selectedSource(), HrSource::None);
    EXPECT_FALSE(a.decide(0).valid);  // no samples after reset
}

// --- External-only mode (External Only setting) -------------------------------

TEST(HrSourceArbiter, ExternalOnlyUsesStrapAndForwardsTrust)
{
    HrSourceArbiter a;
    a.setExternalOnly(true);
    a.submitExternal(132, 1, 1000);   // low trust still used — availability != trust
    auto out = a.decide(1000);
    EXPECT_TRUE(out.valid);
    EXPECT_EQ(out.source, HrSource::External);
    EXPECT_EQ(out.bpm, 132);
    EXPECT_EQ(out.trust, 1);
}

TEST(HrSourceArbiter, ExternalOnlyIgnoresOptical)
{
    HrSourceArbiter a;
    a.setExternalOnly(true);
    a.submitOptical(70, 3, 0);        // optical present and fresh...
    auto out = a.decide(0);
    EXPECT_FALSE(out.valid);           // ...but never selected in external-only
    EXPECT_EQ(out.source, HrSource::None);
}

TEST(HrSourceArbiter, ExternalOnlyGoesInvalidWhenStrapStaleNoOpticalFallback)
{
    HrSourceArbiter a;
    a.setExternalOnly(true);
    a.submitExternal(140, 3, 0);
    a.submitOptical(70, 3, 0);        // wrist available the whole time
    EXPECT_EQ(a.decide(0).source, HrSource::External);
    auto out = a.decide(kFresh + 1);  // strap goes silent
    EXPECT_FALSE(out.valid);           // invalid, NOT a fall back to optical
    EXPECT_EQ(out.source, HrSource::None);
}

TEST(HrSourceArbiter, ClearingExternalOnlyRestoresOpticalFallback)
{
    HrSourceArbiter a;
    a.setExternalOnly(true);
    a.submitOptical(70, 3, 0);
    EXPECT_FALSE(a.decide(0).valid);
    a.setExternalOnly(false);          // back to External & Wrist policy
    auto out = a.decide(0);
    EXPECT_TRUE(out.valid);
    EXPECT_EQ(out.source, HrSource::Optical);
}

// --- clearExternal (accessory release) ----------------------------------------

TEST(HrSourceArbiter, ClearExternalDropsStrapAndFallsBackToOptical)
{
    HrSourceArbiter a;
    a.submitOptical(70, 3, 0);
    a.decide(0);
    a.submitExternal(80, 3, kDwell);
    a.submitOptical(70, 3, kDwell);
    ASSERT_EQ(a.decide(kDwell).source, HrSource::External);

    // Release-time clear: forget the strap. Even though its sample is still
    // within the freshness window, the arbiter must stop preferring it.
    a.clearExternal();
    auto out = a.decide(kDwell + 100);
    EXPECT_EQ(out.source, HrSource::Optical);  // optical still fresh -> fallback
    EXPECT_EQ(out.bpm, 70);
}

TEST(HrSourceArbiter, ClearExternalInExternalOnlyGoesInvalid)
{
    HrSourceArbiter a;
    a.setExternalOnly(true);
    a.submitExternal(132, 3, 0);
    ASSERT_EQ(a.decide(0).source, HrSource::External);

    a.clearExternal();                 // strap forgotten; no optical fallback here
    auto out = a.decide(100);
    EXPECT_FALSE(out.valid);
    EXPECT_EQ(out.source, HrSource::None);
}

// --- decideEx (multi-source HEART_RATE_EX) ------------------------------------

TEST(HrSourceArbiter, DecideExReportsRawOpticalAndExternal)
{
    HrSourceArbiter a;
    a.submitOptical(70, 3, 0);
    a.submitExternal(80, 2, 0);

    auto ex = a.decideEx(0);
    // Arbitrated prefers a fresh strap reading.
    EXPECT_TRUE(ex.arbitrated.valid);
    EXPECT_EQ(ex.arbitrated.source, HrSource::External);
    EXPECT_EQ(ex.arbitrated.bpm, 80);
    // Both raw readings are reported independently.
    EXPECT_TRUE(ex.optical.valid);
    EXPECT_EQ(ex.optical.bpm, 70);
    EXPECT_TRUE(ex.external.valid);
    EXPECT_EQ(ex.external.bpm, 80);

    // Once both age out, every reading reads invalid/zero.
    auto stale = a.decideEx(kFresh + 1);
    EXPECT_FALSE(stale.arbitrated.valid);
    EXPECT_FALSE(stale.optical.valid);
    EXPECT_FALSE(stale.external.valid);
    EXPECT_EQ(stale.optical.bpm, 0);
    EXPECT_EQ(stale.external.bpm, 0);
}

TEST(HrSourceArbiter, DecideExReportsOpticalEvenInExternalOnly)
{
    HrSourceArbiter a;
    a.setExternalOnly(true);          // optical never *arbitrated*...
    a.submitOptical(66, 3, 0);

    auto ex = a.decideEx(0);
    EXPECT_FALSE(ex.arbitrated.valid);          // ...so no arbitrated output
    EXPECT_TRUE(ex.optical.valid);              // ...but it is still logged raw
    EXPECT_EQ(ex.optical.bpm, 66);
    EXPECT_FALSE(ex.external.valid);
}
