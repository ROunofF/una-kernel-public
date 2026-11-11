/**
 ******************************************************************************
 * @file    ProfileHrs.cpp
 * @brief   Heart Rate Service (0x180D) accessory profile.
 ******************************************************************************
 *
 * Pure parsing logic only (no BlueNRG/OS deps) so it is host-unit-tested.
 *
 ******************************************************************************
 */

#include "ProfileHrs.hpp"

namespace Ble
{

bool ProfileHrs::onCharByUuidResp(uint16_t declHandle, const uint8_t *value,
        uint8_t len)
{
    (void)declHandle;  // kept for the interface / diagnostics

    // value = properties(1) | valueHandle(2) | uuid(2 for a 16-bit UUID).
    // Only the 16-bit-UUID form (len == 5) carries the HRS measurement char.
    if (value == nullptr || len < 5) {
        return false;
    }

    uint16_t valueHandle = static_cast<uint16_t>(value[1] |
            (static_cast<uint16_t>(value[2]) << 8));
    uint16_t uuid16 = static_cast<uint16_t>(value[3] |
            (static_cast<uint16_t>(value[4]) << 8));

    if (uuid16 != kMeasurementUuid) {
        return false;
    }

    mValueHandle = valueHandle;
    // The CCCD descriptor sits immediately after the value attribute.
    mCccdHandle = static_cast<uint16_t>(valueHandle + 1);
    return true;
}

bool ProfileHrs::parseNotification(uint16_t attrHandle, const uint8_t *data,
        uint8_t len, AccessorySample &out)
{
    if (data == nullptr || len < 2) {
        return false;
    }

    // Once discovery has recorded our handle, reject notifications for any
    // other attribute. Before discovery (handle 0) parse leniently.
    if (mValueHandle != 0 && attrHandle != mValueHandle) {
        return false;
    }

    uint8_t flags = data[0];

    uint16_t bpm;
    if (flags & kFlagValue16Bit) {
        if (len < 3) {
            return false;
        }
        bpm = static_cast<uint16_t>(data[1] |
                (static_cast<uint16_t>(data[2]) << 8));
    } else {
        bpm = data[1];
    }

    // Reject the warm-up / invalid 0 BPM reading.
    if (bpm == 0) {
        return false;
    }

    bool contactSupported = (flags & kFlagContactSupported) != 0;
    bool contactDetected  = (flags & kFlagContactDetected) != 0;

    out.kind  = AccessorySample::Kind::HeartRate;
    out.value = bpm;
    // Contact not supported is treated as good (many straps don't report it);
    // supported-but-absent means the strap is off-body -> degraded trust.
    out.trust = (!contactSupported || contactDetected) ? kTrustContactGood
                                                       : kTrustContactLost;
    return true;
}

void ProfileHrs::reset()
{
    mValueHandle = 0;
    mCccdHandle = 0;
}

}  /* namespace Ble */
