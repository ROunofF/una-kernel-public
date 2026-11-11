/**
 ******************************************************************************
 * @file    IAccessoryProfile.hpp
 * @brief   GATT profile contract for a Ble::AccessoryCentral link.
 ******************************************************************************
 *
 * An accessory profile knows the UUIDs to discover on a connected accessory,
 * records the handles found during discovery, and turns raw notifications into
 * a typed sample. It owns no BlueNRG/OS state, so the parsing logic is exercised
 * by host unit tests. AccessoryCentral drives the actual ACI scan/connect/
 * discover/subscribe; the profile only describes *what* to discover and *how*
 * to interpret the bytes.
 *
 * v1 ships ProfileHrs (Heart Rate Service 0x180D / 0x2A37). Future cadence
 * (0x1816) / power (0x1818) profiles are additional implementations -- no
 * transport change.
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_IACCESSORY_PROFILE_HPP
#define __COMPONENT_BLE_IACCESSORY_PROFILE_HPP

#include <cstdint>

namespace Ble
{

/**
 * @brief   Tick rate of a revolution event-time counter.
 *
 * Both CPS and CSC timestamp CRANK events in 1/1024 s. WHEEL events differ:
 * CPS uses 1/2048 s, CSC uses 1/1024 s. Mistaking one for the other scales
 * every derived rate by exactly two, so the profile states it per sample
 * rather than leaving the consumer to assume.
 */
enum class RevTickRate : uint8_t {
    Hz1024 = 0,
    Hz2048 = 1,
};

/**
 * @brief   A typed measurement decoded from an accessory notification.
 *
 * The HeartRate fields (value/trust) keep their original meaning byte for byte,
 * so ProfileHrs and the whole HR path are unaffected by the cycling additions.
 * This struct is kernel-internal -- it is NOT an app-facing message -- so growing
 * it carries no ABI risk (contrast SDK/Messages, which must never grow).
 */
struct AccessorySample {
    enum class Kind : uint8_t {
        HeartRate,      // value = beats per minute
        CyclingPower,   // CPS 0x1818: power, plus optional crank/wheel counters
        CyclingRevs,    // CSC 0x1816: crank and/or wheel counters, no power
    };

    /**
     * @brief   Which optional cycling fields this sample actually carries.
     *
     * Both CPS and CSC are flag-gated and a conforming device may omit any
     * optional field on any notification, so presence is per sample and must
     * never be inferred from the device's declared capabilities.
     */
    enum Field : uint8_t {
        FieldPower = 1u << 0,
        FieldCrank = 1u << 1,
        FieldWheel = 1u << 2,
    };

    Kind     kind  { Kind::HeartRate };
    uint16_t value { 0 };   // measurement (e.g. BPM for HeartRate)
    // Trust level using the downstream sensor-layer scale: 3 = stable/good,
    // 1 = degraded (e.g. sensor contact lost). See HrSourceArbiter.
    uint8_t  trust { 0 };

    // --- Cycling payload; meaningful only for the cycling kinds -----------
    uint8_t  fields     { 0 };   // bitmask of Field; unmarked fields are unset
    int16_t  powerWatts { 0 };   // CPS instantaneous power -- SIGNED per spec

    // Cumulative revolution counters, widened to uint32 for the estimator.
    // On the wire crank is uint16 while wheel is uint32 (a spec asymmetry that
    // is easy to miss), so rollover must be applied at each counter's own
    // modulus -- see RevolutionRateEstimator::CounterWidth.
    uint32_t    crankRevs       { 0 };
    uint16_t    crankEventTicks { 0 };   // always 1/1024 s, CPS and CSC alike
    uint32_t    wheelRevs       { 0 };
    uint16_t    wheelEventTicks { 0 };   // rate given by wheelTickRate
    RevTickRate wheelTickRate   { RevTickRate::Hz1024 };
};

/**
 * @brief   Decode a char-by-uuid discovery response and match one UUID.
 *
 * Every profile needs the identical few lines to pick its characteristic out of
 * an `una_ble_gatt_disc_read_char_by_uuid_resp_event`, so they live here once.
 * ProfileHrs predates this helper and still open-codes it; folding it in belongs
 * with a profile refactor rather than here, to leave shipped BLE code alone.
 *
 * @param   value: declaration value = properties(1) | valueHandle(2) | uuid(2).
 * @param   len: length of value in bytes.
 * @param   wantUuid: the 16-bit characteristic UUID being looked for.
 * @param   valueHandleOut: set to the value handle only on a match.
 * @retval  'true' if this declaration is the wanted characteristic.
 */
inline bool matchCharDeclaration(const uint8_t *value, uint8_t len,
        uint16_t wantUuid, uint16_t &valueHandleOut)
{
    // Only the 16-bit-UUID form (len >= 5) can name a SIG characteristic; a
    // 128-bit declaration is 19 bytes and never one of ours.
    if (value == nullptr || len < 5) {
        return false;
    }
    const uint16_t handle = static_cast<uint16_t>(value[1] |
            (static_cast<uint16_t>(value[2]) << 8));
    const uint16_t uuid16 = static_cast<uint16_t>(value[3] |
            (static_cast<uint16_t>(value[4]) << 8));
    if (uuid16 != wantUuid) {
        return false;
    }
    valueHandleOut = handle;
    return true;
}

/**
 * @brief   GATT profile contract for one accessory kind.
 */
class IAccessoryProfile {
public:

    /**
     * @brief   16-bit primary service UUID to scan/connect for (e.g. 0x180D).
     */
    virtual uint16_t serviceUuid() const = 0;

    /**
     * @brief   16-bit characteristic UUID to subscribe to (e.g. 0x2A37).
     */
    virtual uint16_t characteristicUuid() const = 0;

    /**
     * @brief   Feed a read-characteristic-by-UUID discovery response. Records
     *          the value/CCCD handles if this is the profile's characteristic.
     * @param   declHandle: declaration handle (the event's Attribute_Handle).
     * @param   value: declaration value = properties(1) | valueHandle(2) |
     *          uuid(2 for a 16-bit UUID).
     * @param   len: length of value in bytes.
     * @retval  'true' if the profile characteristic was matched and recorded.
     */
    virtual bool onCharByUuidResp(uint16_t declHandle, const uint8_t *value,
            uint8_t len) = 0;

    /**
     * @brief   Discovered value-attribute handle, or 0 before discovery.
     */
    virtual uint16_t valueHandle() const = 0;

    /**
     * @brief   CCCD handle to write to enable notifications (valueHandle + 1),
     *          or 0 before discovery.
     */
    virtual uint16_t cccdHandle() const = 0;

    /**
     * @brief   Decode a notification into a typed sample.
     * @param   attrHandle: attribute handle the notification arrived on.
     * @param   data: notification payload.
     * @param   len: length of data in bytes.
     * @param   out: filled with the decoded sample when the return is 'true'.
     * @retval  'true' if a usable sample was produced; 'false' to drop it
     *          (wrong handle, malformed, or a warm-up/invalid reading).
     */
    virtual bool parseNotification(uint16_t attrHandle, const uint8_t *data,
            uint8_t len, AccessorySample &out) = 0;

    /**
     * @brief   Forget discovered handles (on disconnect / re-acquire).
     */
    virtual void reset() = 0;

protected:
    virtual ~IAccessoryProfile() = default;
};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_IACCESSORY_PROFILE_HPP */
