/**
 ******************************************************************************
 * @file    ProfileHrs.hpp
 * @brief   Heart Rate Service (0x180D) accessory profile.
 ******************************************************************************
 *
 * First IAccessoryProfile implementation: discovers the Heart Rate Measurement
 * characteristic (0x2A37) and decodes its notifications (flags + 8/16-bit BPM +
 * sensor-contact bits) into a typed sample. Pure logic — no BlueNRG/OS deps —
 * so it is exercised directly by host unit tests.
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_PROFILE_HRS_HPP
#define __COMPONENT_BLE_PROFILE_HRS_HPP

#include <cstdint>

#include "IAccessoryProfile.hpp"

namespace Ble
{

/**
 * @brief   Heart Rate Service profile (0x180D / 0x2A37).
 */
class ProfileHrs : public IAccessoryProfile {
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

    static constexpr uint16_t kServiceUuid      = 0x180D;
    static constexpr uint16_t kMeasurementUuid  = 0x2A37;

    // Heart Rate Measurement flags (0x2A37), first byte.
    static constexpr uint8_t kFlagValue16Bit       = 0x01; // BPM is uint16 (LE)
    static constexpr uint8_t kFlagContactDetected  = 0x02;
    static constexpr uint8_t kFlagContactSupported = 0x04;

    // Trust levels on the downstream sensor-layer scale (see HrSourceArbiter):
    // contact good / not reported -> stable; contact supported but absent ->
    // degraded.
    static constexpr uint8_t kTrustContactGood = 3;
    static constexpr uint8_t kTrustContactLost = 1;

    uint16_t mValueHandle { 0 };  // 0x2A37 value attribute handle
    uint16_t mCccdHandle  { 0 };  // its CCCD (valueHandle + 1)
};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_PROFILE_HRS_HPP */
