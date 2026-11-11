/**
 ******************************************************************************
 * @file    ProfileCps.hpp
 * @brief   Cycling Power Service (0x1818) accessory profile.
 ******************************************************************************
 *
 * Decodes Cycling Power Measurement (0x2A63) notifications into instantaneous
 * power plus, when present, raw cumulative wheel and/or crank counters. It
 * derives no rate: cadence and wheel rate come from RevolutionRateEstimator.
 *
 * TWO TRAPS THIS PROFILE EXISTS TO GET RIGHT
 *
 * 1. The Flags field is **16-bit** here, where HRS uses 8. Reading one byte
 *    would shift every subsequent field by one.
 *
 * 2. Optional fields appear in FLAG-BIT ORDER, and two of them sit BEFORE the
 *    wheel/crank data we want. They must be stepped over even though we never
 *    use them, or the counters are read from the wrong offset -- which yields
 *    plausible-looking nonsense rather than an error:
 *
 *      Flags                  u16   always
 *      Instantaneous Power    s16   always (SIGNED by spec)
 *      Pedal Power Balance    u8    bit 0   <-- must skip
 *      Accumulated Torque     u16   bit 2   <-- must skip
 *      Wheel Revolution Data  u32+u16  bit 4  (event time 1/2048 s)
 *      Crank Revolution Data  u16+u16  bit 5  (event time 1/1024 s)
 *      ... extreme force/torque/angles, dead spots, energy (bits 6-11)
 *
 *    Everything after the crank block is ignored, which is safe because we never
 *    read past it. Bits 1, 3 and 12 are indicator bits carrying no payload.
 *
 * Note the wheel event time is **1/2048 s** in CPS but 1/1024 s in CSC, so the
 * tick rate travels with the sample rather than being assumed downstream. Getting
 * it wrong scales the derived wheel rate by exactly two.
 *
 * The device's Cycling Power Feature (0x2A65) characteristic is deliberately NOT
 * read: capability is inferred from the per-notification Flags, which is ground
 * truth -- a device can advertise crank support in Feature and then not deliver
 * it. That also keeps this profile to a single characteristic, needing no GATT
 * read path in the accessory framework.
 *
 * Pure parsing logic -- no BlueNRG/OS deps -- so it is host-unit-tested.
 *
 ******************************************************************************
 */

#ifndef PROFILE_CPS_HPP
#define PROFILE_CPS_HPP

#include <cstdint>

#include "IAccessoryProfile.hpp"

namespace Ble
{

/**
 * @brief   Cycling Power profile (0x1818 / 0x2A63).
 */
class ProfileCps : public IAccessoryProfile {
public:

    uint16_t serviceUuid() const override { return kServiceUuid; }
    uint16_t characteristicUuid() const override { return kMeasurementUuid; }

    bool onCharByUuidResp(uint16_t declHandle, const uint8_t *value,
            uint8_t len) override;

    uint16_t valueHandle() const override { return mValueHandle; }
    uint16_t cccdHandle() const override { return mCccdHandle; }

    bool parseNotification(uint16_t attrHandle, const uint8_t *data,
            uint8_t len, AccessorySample &out) override;

    void reset() override;

private:

    static constexpr uint16_t kServiceUuid     = 0x1818;
    static constexpr uint16_t kMeasurementUuid = 0x2A63;

    // Cycling Power Measurement flags (16-bit). Only the bits that gate a
    // payload we must consume or skip are named; the rest carry no data.
    static constexpr uint16_t kFlagPedalPowerBalance = 0x0001;  // bit 0,  u8
    static constexpr uint16_t kFlagAccumulatedTorque = 0x0004;  // bit 2,  u16
    static constexpr uint16_t kFlagWheelRevsPresent  = 0x0010;  // bit 4,  u32+u16
    static constexpr uint16_t kFlagCrankRevsPresent  = 0x0020;  // bit 5,  u16+u16

    uint16_t mValueHandle { 0 };  // 0x2A63 value attribute handle
    uint16_t mCccdHandle  { 0 };  // its CCCD (valueHandle + 1)
};

}  /* namespace Ble */

#endif /* PROFILE_CPS_HPP */
