/**
 ******************************************************************************
 * @file    ProfileCps.cpp
 * @brief   Cycling Power Service (0x1818) accessory profile.
 ******************************************************************************
 *
 * Pure parsing logic only (no BlueNRG/OS deps) so it is host-unit-tested.
 * See the header for the two field-layout traps this exists to get right.
 *
 ******************************************************************************
 */

#include "ProfileCps.hpp"

#include "AccessoryPayloadReader.hpp"

namespace Ble
{

namespace {

// Trust on the downstream sensor-layer scale. CPS carries no quality signal, so
// a decoded sample is simply "good"; staleness invalidates cycling metrics.
constexpr uint8_t kTrustGood = 3;

// Payload sizes of the optional fields that PRECEDE wheel/crank data and so must
// be stepped over when present.
constexpr uint8_t kPedalPowerBalanceBytes = 1;
constexpr uint8_t kAccumulatedTorqueBytes = 2;

}  // namespace

bool ProfileCps::onCharByUuidResp(uint16_t declHandle, const uint8_t *value,
        uint8_t len)
{
    (void)declHandle;  // kept for the interface / diagnostics

    uint16_t valueHandle = 0;
    if (!matchCharDeclaration(value, len, kMeasurementUuid, valueHandle)) {
        return false;
    }

    mValueHandle = valueHandle;
    // The CCCD descriptor sits immediately after the value attribute.
    mCccdHandle = static_cast<uint16_t>(valueHandle + 1);
    return true;
}

bool ProfileCps::parseNotification(uint16_t attrHandle, const uint8_t *data,
        uint8_t len, AccessorySample &out)
{
    // Once discovery has recorded our handle, reject notifications for any
    // other attribute. Before discovery (handle 0) parse leniently.
    if (mValueHandle != 0 && attrHandle != mValueHandle) {
        return false;
    }

    PayloadReader r(data, len);

    // Flags are 16-bit in CPS (8-bit in HRS -- an easy and costly confusion).
    const uint16_t flags = r.u16();
    // Instantaneous power is mandatory and signed: a trainer coasting can
    // legitimately report a small negative value.
    const int16_t power = r.i16();
    if (!r.ok()) {
        return false;   // a frame too short for flags + power is malformed
    }

    AccessorySample sample {};
    sample.kind       = AccessorySample::Kind::CyclingPower;
    sample.trust      = kTrustGood;
    sample.powerWatts = power;
    sample.fields     = AccessorySample::FieldPower;

    // Step over the optional fields that precede wheel/crank. Skipping these
    // wrongly is the classic CPS bug: the counters then decode from the wrong
    // offset and produce plausible garbage instead of a parse failure.
    if ((flags & kFlagPedalPowerBalance) != 0) {
        r.skip(kPedalPowerBalanceBytes);
    }
    if ((flags & kFlagAccumulatedTorque) != 0) {
        r.skip(kAccumulatedTorqueBytes);
    }

    if ((flags & kFlagWheelRevsPresent) != 0) {
        const uint32_t revs  = r.u32();
        const uint16_t ticks = r.u16();
        if (!r.ok()) {
            return false;
        }
        sample.wheelRevs       = revs;
        sample.wheelEventTicks = ticks;
        // CPS times the WHEEL at 1/2048 s, unlike CSC's 1/1024 s.
        sample.wheelTickRate   = RevTickRate::Hz2048;
        sample.fields         |= AccessorySample::FieldWheel;
    }

    if ((flags & kFlagCrankRevsPresent) != 0) {
        const uint16_t revs  = r.u16();
        const uint16_t ticks = r.u16();
        if (!r.ok()) {
            return false;
        }
        sample.crankRevs       = revs;
        sample.crankEventTicks = ticks;
        sample.fields         |= AccessorySample::FieldCrank;
    }

    // Fields after the crank block (extreme force/torque/angles, dead-spot
    // angles, accumulated energy) are never read, so their presence is harmless
    // and needs no handling.

    // Catches a truncated skip() as well as a truncated read: any overrun
    // latches in the reader, so one check covers the whole walk.
    if (!r.ok()) {
        return false;
    }

    out = sample;
    return true;
}

void ProfileCps::reset()
{
    mValueHandle = 0;
    mCccdHandle = 0;
}

}  /* namespace Ble */
