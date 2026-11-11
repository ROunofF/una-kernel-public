/**
 ******************************************************************************
 * @file    ProfileCsc.hpp
 * @brief   Cycling Speed and Cadence Service (0x1816) accessory profile.
 ******************************************************************************
 *
 * Decodes CSC Measurement (0x2A5B) notifications into raw cumulative wheel
 * and/or crank counters. It derives no rate: cadence and wheel rate come from
 * RevolutionRateEstimator, which owns the delta-revs/delta-time arithmetic and
 * its rollover and stall handling for CPS and CSC alike.
 *
 * A device may report wheel data, crank data, or both -- the two flags are
 * independent. Verified on hardware: a Garmin Speed Sensor 2 sends wheel only (7-byte frame, flags
 * 0x01) and a Cadence Sensor 2 sends crank only (5-byte frame, flags 0x02), so
 * BOTH single-field cases are first-class, not degenerate.
 *
 * Note the counter widths differ, which is a spec asymmetry and an easy
 * off-by-two: cumulative WHEEL revolutions are uint32, cumulative CRANK
 * revolutions are uint16. Both event times are uint16 at 1/1024 s.
 *
 * Pure parsing logic -- no BlueNRG/OS deps -- so it is host-unit-tested against
 * payloads captured from real hardware.
 *
 ******************************************************************************
 */

#ifndef PROFILE_CSC_HPP
#define PROFILE_CSC_HPP

#include <cstdint>

#include "IAccessoryProfile.hpp"

namespace Ble
{

/**
 * @brief   Cycling Speed and Cadence profile (0x1816 / 0x2A5B).
 */
class ProfileCsc : public IAccessoryProfile {
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

    static constexpr uint16_t kServiceUuid     = 0x1816;
    static constexpr uint16_t kMeasurementUuid = 0x2A5B;

    // CSC Measurement flags, first (and only) flags byte.
    static constexpr uint8_t kFlagWheelRevsPresent = 0x01;  // bit 0
    static constexpr uint8_t kFlagCrankRevsPresent = 0x02;  // bit 1

    uint16_t mValueHandle { 0 };  // 0x2A5B value attribute handle
    uint16_t mCccdHandle  { 0 };  // its CCCD (valueHandle + 1)
};

}  /* namespace Ble */

#endif /* PROFILE_CSC_HPP */
