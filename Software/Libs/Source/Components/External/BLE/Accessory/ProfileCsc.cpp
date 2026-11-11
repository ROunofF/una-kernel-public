/**
 ******************************************************************************
 * @file    ProfileCsc.cpp
 * @brief   Cycling Speed and Cadence Service (0x1816) accessory profile.
 ******************************************************************************
 *
 * Pure parsing logic only (no BlueNRG/OS deps) so it is host-unit-tested.
 *
 ******************************************************************************
 */

#include "ProfileCsc.hpp"

#include "AccessoryPayloadReader.hpp"

namespace Ble
{

namespace {

// Trust on the downstream sensor-layer scale. CSC carries no quality signal at
// all -- unlike HRS, which reports sensor contact -- so a decoded sample is
// simply "good"; staleness, not trust, is what invalidates cycling metrics
// (see RevolutionRateEstimator).
constexpr uint8_t kTrustGood = 3;

}  // namespace

bool ProfileCsc::onCharByUuidResp(uint16_t declHandle, const uint8_t *value,
        uint8_t len)
{
    (void)declHandle;  // kept for the interface / diagnostics

    uint16_t valueHandle = 0;
    if (!matchCharDeclaration(value, len, kMeasurementUuid, valueHandle)) {
        return false;
    }

    mValueHandle = valueHandle;
    // The CCCD descriptor sits immediately after the value attribute. Verified
    // on a fourth vendor (Garmin CSC) alongside the three validated HR straps.
    mCccdHandle = static_cast<uint16_t>(valueHandle + 1);
    return true;
}

bool ProfileCsc::parseNotification(uint16_t attrHandle, const uint8_t *data,
        uint8_t len, AccessorySample &out)
{
    // Once discovery has recorded our handle, reject notifications for any
    // other attribute. Before discovery (handle 0) parse leniently.
    if (mValueHandle != 0 && attrHandle != mValueHandle) {
        return false;
    }

    PayloadReader r(data, len);
    const uint8_t flags = r.u8();
    if (!r.ok()) {
        return false;
    }

    AccessorySample sample {};
    sample.kind  = AccessorySample::Kind::CyclingRevs;
    sample.trust = kTrustGood;

    // Field order follows flag-bit order: wheel data (bit 0) then crank (bit 1).
    if ((flags & kFlagWheelRevsPresent) != 0) {
        const uint32_t revs  = r.u32();
        const uint16_t ticks = r.u16();
        if (!r.ok()) {
            return false;
        }
        sample.wheelRevs       = revs;
        sample.wheelEventTicks = ticks;
        // CSC times BOTH wheel and crank at 1/1024 s. CPS differs on the wheel
        // (1/2048 s), which is why the rate is carried per sample.
        sample.wheelTickRate   = RevTickRate::Hz1024;
        sample.fields         |= AccessorySample::FieldWheel;
    }

    // Crank revolutions are uint16 here, where wheel revolutions are uint32.
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

    // A frame with neither flag set is well-formed but carries no measurement;
    // drop it rather than publish an empty sample.
    if (sample.fields == 0) {
        return false;
    }

    out = sample;
    return true;
}

void ProfileCsc::reset()
{
    mValueHandle = 0;
    mCccdHandle = 0;
}

}  /* namespace Ble */
