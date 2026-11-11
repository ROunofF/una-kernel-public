/**
 ******************************************************************************
 * @file    ConnParamNegotiation.hpp
 * @brief   Resolve a peer's connection-parameter request against our band.
 ******************************************************************************
 *
 * Pure arithmetic, deliberately free of BlueNRG headers so it can be host
 * tested. AccessoryLink owns the policy (the band) and the ACI calls; this
 * decides only what the answer should be.
 *
 * WHY A BAND RATHER THAN THE PEER'S REQUEST. Measured across four straps, no
 * single window satisfies everyone: COROS asks 7-20 ms, Coospo 125-250 ms,
 * Garmin 240-260 ms, Polar H10 311-500 ms. Granting each request verbatim would
 * drag the link to ~30 ms for the COROS -- roughly 13x the wake rate a 1-4 Hz
 * profile needs, and ruinous for the slot budget once several sensors are
 * linked. So the band is the policy: intersect the request with it where they
 * overlap, otherwise use the band.
 *
 * WHAT THESE VALUES ACTUALLY DO. The L2CAP Connection Parameter Update Response
 * carries only a Result field (Core Vol 3 Part A 4.21), so the intervals are
 * never transmitted -- the peer reads the real values from
 * LL_CONNECTION_UPDATE_IND. They are not wasted, though: measured on BlueNRG-2,
 * the controller starts a link-layer update from the accept
 * carrying exactly these parameters. So this result is what the link ends up
 * RUNNING, and steering the controller is how the peer finds out.
 *
 * Which is why the caller must also apply what it answers rather than treating
 * the answer as advisory: a peer told "accepted" and then run at something else
 * is the condition that makes a Polar H10 fall silent while holding the link up.
 *
 ******************************************************************************
 */

#ifndef COMPONENT_BLE_CONN_PARAM_NEGOTIATION_HPP
#define COMPONENT_BLE_CONN_PARAM_NEGOTIATION_HPP

#include <cstdint>

namespace Ble
{

/**
 * @brief   A connection-parameter set in controller units.
 * @note    interval is 1.25 ms per unit, supTimeout is 10 ms per unit, latency
 *          is a count of skippable connection events.
 */
struct ConnParams {
    uint16_t intervalMin { 0 };
    uint16_t intervalMax { 0 };
    uint16_t latency { 0 };
    uint16_t supTimeout { 0 };
};

/**
 * @brief   The window we are willing to run, and what we impose within it.
 */
struct ConnParamBand {
    uint16_t intervalMin { 0 };     ///< inclusive lower bound
    uint16_t intervalMax { 0 };     ///< inclusive upper bound
    uint16_t latencyMax { 0 };      ///< most peripheral latency we will grant
    uint16_t supTimeout { 0 };      ///< supervision timeout we impose
    uint16_t supTimeoutMax { 0 };   ///< controller ceiling for the timeout
};

/**
 * @brief   Outcome of resolving a request against a band.
 */
struct ConnParamAnswer {
    ConnParams params { };
    bool accept { false };
};

/**
 * @brief   Resolve a peer's request against our band.
 * @param   req: the peer's requested parameters.
 * @param   band: the window we are willing to run.
 * @retval  The parameters to answer with AND to apply, plus accept/reject.
 * @note    A request whose interval range is inverted (min > max) is treated
 *          exactly like a disjoint one. Without that, an inverted range whose
 *          endpoints both sit inside the band would pass the disjoint test with
 *          neither clamp firing, and the inversion would survive into an
 *          accepted answer whose link-layer update is then rejected
 *          invalid-params -- telling the peer yes and applying nothing.
 */
inline ConnParamAnswer negotiateConnParams(const ConnParams &req,
        const ConnParamBand &band)
{
    ConnParamAnswer out {};

    const bool malformed = (req.intervalMin > req.intervalMax);
    const bool disjoint  = (req.intervalMax < band.intervalMin)
                        || (req.intervalMin > band.intervalMax);

    if (malformed || disjoint) {
        out.params.intervalMin = band.intervalMin;
        out.params.intervalMax = band.intervalMax;
    } else {
        out.params.intervalMin = (req.intervalMin < band.intervalMin)
                ? band.intervalMin : req.intervalMin;
        out.params.intervalMax = (req.intervalMax > band.intervalMax)
                ? band.intervalMax : req.intervalMax;
    }

    // Latency is the peer's power lever, but it multiplies the delivery gap:
    // interval x (latency + 1) has to stay inside a 1 Hz notification period,
    // so cap it at what we already run rather than granting more.
    out.params.latency = (req.latency > band.latencyMax) ? band.latencyMax
                                                         : req.latency;

    // Core Vol 6 Part B 4.5.2: supervision timeout must EXCEED
    // (1 + latency) * intervalMax * 2. Interval is in 1.25 ms units and the
    // timeout in 10 ms units, so the comparison scales by 8. Widen rather than
    // reject: a peer asking for a legal interval with a marginal timeout still
    // deserves its interval.
    out.params.supTimeout = band.supTimeout;
    const uint32_t minSupTimeout =
            ((1u + out.params.latency)
                    * static_cast<uint32_t>(out.params.intervalMax) * 2u) / 8u
            + 1u;
    if (out.params.supTimeout < minSupTimeout) {
        out.params.supTimeout = (minSupTimeout > band.supTimeoutMax)
                ? band.supTimeoutMax
                : static_cast<uint16_t>(minSupTimeout);
    }

    // Reject only if no legal pairing survived. With a sane band this holds by
    // construction; the guard exists so a future band change cannot ship an
    // out-of-spec answer silently.
    out.accept = ((1u + out.params.latency)
                    * static_cast<uint32_t>(out.params.intervalMax) * 2u) / 8u
            < out.params.supTimeout;

    return out;
}

}  /* namespace Ble */

#endif  // COMPONENT_BLE_CONN_PARAM_NEGOTIATION_HPP
