/**
 ******************************************************************************
 * @file    ConnParamNegotiation_test.cpp
 * @brief   Host tests for resolving a peer's conn-param request against a band.
 ******************************************************************************
 *
 * This arithmetic used to be inline in AccessoryLink, untestable behind
 * BlueNRG headers. The cases named after real straps are requests measured
 * from those straps, so a change in policy shows up here first.
 *
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include "ConnParamNegotiation.hpp"

using Ble::ConnParamAnswer;
using Ble::ConnParamBand;
using Ble::ConnParams;
using Ble::negotiateConnParams;

namespace {

// The shipped band: 249-400 units (311.25-500 ms), latency <= 1, supervision
// 6000 ms, controller ceiling 32000 ms.
constexpr ConnParamBand kBand { 249, 400, 1, 600, 3200 };

ConnParams req(uint16_t lo, uint16_t hi, uint16_t lat = 0, uint16_t sup = 500)
{
    return ConnParams { lo, hi, lat, sup };
}

/// The Core Vol 6 Part B 4.5.2 relation the answer must always satisfy.
bool supervisionIsLegal(const ConnParams &p)
{
    return ((1u + p.latency) * static_cast<uint32_t>(p.intervalMax) * 2u) / 8u
            < p.supTimeout;
}

}  // namespace

// --- the four measured straps ----------------------------------------------

TEST(ConnParamNegotiation, CorosRequestIsDisjointBelowAndGetsTheBand)
{
    // COROS asks 7-20 ms: far below the band, so it is answered with the band
    // rather than dragged to 30 ms, which would be ~13x the wake rate needed.
    const ConnParamAnswer a = negotiateConnParams(req(6, 16, 0), kBand);

    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 400);
    EXPECT_EQ(a.params.latency, 0);
    EXPECT_TRUE(a.accept);
}

TEST(ConnParamNegotiation, CoospoRequestIsDisjointBelowAndGetsTheBand)
{
    // Coospo asks 125-250 ms latency 2; latency is capped at the band's 1.
    const ConnParamAnswer a = negotiateConnParams(req(100, 200, 2), kBand);

    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 400);
    EXPECT_EQ(a.params.latency, 1);
    EXPECT_TRUE(a.accept);
}

TEST(ConnParamNegotiation, GarminRequestIsDisjointBelowAndGetsTheBand)
{
    // Garmin asks 240-260 ms -- close to the band but not overlapping it.
    const ConnParamAnswer a = negotiateConnParams(req(192, 208, 0), kBand);

    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 400);
    EXPECT_TRUE(a.accept);
}

TEST(ConnParamNegotiation, PolarH10RequestMatchesTheBandExactly)
{
    // The H10's 311-500 ms latency 1 IS the band, which is why it stops asking
    // once the relax has been applied.
    const ConnParamAnswer a = negotiateConnParams(req(249, 400, 1), kBand);

    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 400);
    EXPECT_EQ(a.params.latency, 1);
    EXPECT_TRUE(a.accept);
}

// --- overlap and clamping ---------------------------------------------------

TEST(ConnParamNegotiation, ContainedRequestIsHonouredExactly)
{
    // Wholly inside the band: the peer gets what it asked for.
    const ConnParamAnswer a = negotiateConnParams(req(300, 350, 1), kBand);

    EXPECT_EQ(a.params.intervalMin, 300);
    EXPECT_EQ(a.params.intervalMax, 350);
    EXPECT_TRUE(a.accept);
}

TEST(ConnParamNegotiation, RequestStraddlingTheLowerEdgeIsClampedUp)
{
    const ConnParamAnswer a = negotiateConnParams(req(100, 300), kBand);

    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 300);
}

TEST(ConnParamNegotiation, RequestStraddlingTheUpperEdgeIsClampedDown)
{
    const ConnParamAnswer a = negotiateConnParams(req(350, 900), kBand);

    EXPECT_EQ(a.params.intervalMin, 350);
    EXPECT_EQ(a.params.intervalMax, 400);
}

TEST(ConnParamNegotiation, RequestEnclosingTheBandCollapsesToTheBand)
{
    const ConnParamAnswer a = negotiateConnParams(req(10, 900), kBand);

    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 400);
}

TEST(ConnParamNegotiation, TouchingTheLowerEdgeCountsAsOverlap)
{
    // req.max == band.min: a degenerate but legal single-point answer, not a
    // disjoint one. The boundary is inclusive on both sides.
    const ConnParamAnswer a = negotiateConnParams(req(100, 249), kBand);

    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 249);
    EXPECT_LE(a.params.intervalMin, a.params.intervalMax);
    EXPECT_TRUE(a.accept);
}

TEST(ConnParamNegotiation, TouchingTheUpperEdgeCountsAsOverlap)
{
    const ConnParamAnswer a = negotiateConnParams(req(400, 900), kBand);

    EXPECT_EQ(a.params.intervalMin, 400);
    EXPECT_EQ(a.params.intervalMax, 400);
    EXPECT_TRUE(a.accept);
}

TEST(ConnParamNegotiation, DisjointAboveGetsTheBand)
{
    const ConnParamAnswer a = negotiateConnParams(req(500, 800), kBand);

    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 400);
    EXPECT_TRUE(a.accept);
}

// --- malformed input --------------------------------------------------------

TEST(ConnParamNegotiation, InvertedRangeInsideTheBandDoesNotSurvive)
{
    // The review finding: min > max with BOTH endpoints inside the band passed
    // the disjoint test with neither clamp firing, so the inversion reached an
    // accepted answer whose link-layer update is rejected invalid-params --
    // telling the peer yes and applying nothing.
    const ConnParamAnswer a = negotiateConnParams(req(300, 260), kBand);

    EXPECT_LE(a.params.intervalMin, a.params.intervalMax);
    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 400);
    EXPECT_TRUE(a.accept);
}

TEST(ConnParamNegotiation, InvertedRangeOutsideTheBandAlsoGetsTheBand)
{
    const ConnParamAnswer a = negotiateConnParams(req(900, 10), kBand);

    EXPECT_EQ(a.params.intervalMin, 249);
    EXPECT_EQ(a.params.intervalMax, 400);
}

TEST(ConnParamNegotiation, OutputIsNeverInvertedAcrossASweep)
{
    for (uint16_t lo = 0; lo < 900; lo += 37) {
        for (uint16_t hi = 0; hi < 900; hi += 41) {
            for (uint16_t lat = 0; lat <= 5; ++lat) {
                const ConnParamAnswer a =
                        negotiateConnParams(req(lo, hi, lat), kBand);
                EXPECT_LE(a.params.intervalMin, a.params.intervalMax)
                        << "lo=" << lo << " hi=" << hi << " lat=" << lat;
                EXPECT_LE(a.params.latency, kBand.latencyMax);
                if (a.accept) {
                    EXPECT_TRUE(supervisionIsLegal(a.params))
                            << "lo=" << lo << " hi=" << hi << " lat=" << lat;
                }
            }
        }
    }
}

// --- latency and supervision -------------------------------------------------

TEST(ConnParamNegotiation, LatencyIsCappedAtTheBand)
{
    EXPECT_EQ(negotiateConnParams(req(300, 350, 7), kBand).params.latency, 1);
    EXPECT_EQ(negotiateConnParams(req(300, 350, 1), kBand).params.latency, 1);
    EXPECT_EQ(negotiateConnParams(req(300, 350, 0), kBand).params.latency, 0);
}

TEST(ConnParamNegotiation, SupervisionComesFromTheBandNotThePeer)
{
    // The peer's 2010 ms request (Garmin) does not move our imposed timeout.
    const ConnParamAnswer a = negotiateConnParams(req(300, 350, 0, 201), kBand);

    EXPECT_EQ(a.params.supTimeout, 600);
}

TEST(ConnParamNegotiation, SupervisionIsWidenedWhenTheBandWouldBeIllegal)
{
    // A band whose supervision is too short for its own interval must be
    // widened, not rejected: (1 + 1) * 400 * 2 / 8 = 200, so 100 is illegal.
    constexpr ConnParamBand tight { 249, 400, 1, 100, 3200 };
    const ConnParamAnswer a = negotiateConnParams(req(249, 400, 1), tight);

    EXPECT_GT(a.params.supTimeout, 200);
    EXPECT_TRUE(a.accept);
    EXPECT_TRUE(supervisionIsLegal(a.params));
}

TEST(ConnParamNegotiation, RejectsWhenTheCeilingCannotSatisfyTheRelation)
{
    // Ceiling below the required minimum: no legal pairing exists, so the guard
    // must reject rather than emit an out-of-spec answer.
    constexpr ConnParamBand impossible { 3000, 3200, 4, 10, 20 };
    const ConnParamAnswer a = negotiateConnParams(req(3000, 3200, 4), impossible);

    EXPECT_FALSE(a.accept);
}
