/**
 ******************************************************************************
 * @file    IGattClient.hpp
 * @brief   Interface for a service client driven by the GATT-client coordinator.
 ******************************************************************************
 *
 * A service client declares the service and characteristics it is interested in
 * and receives the resolved handles once discovery completes. The coordinator
 * discovers registered clients one at a time and routes notifications back to
 * the client that owns the notifying handle. All callbacks run on the BLE event
 * thread: link events arrive there, and timer-driven setup steps are marshalled
 * onto it (see IBleThreadWork), so a client's state needs no locking.
 *
 ******************************************************************************
 */

#ifndef BLE_DISCOVERY_IGATT_CLIENT_HPP
#define BLE_DISCOVERY_IGATT_CLIENT_HPP

#include <cstdint>

#include "IBle.hpp"

namespace Ble
{

/**
 * @brief   A characteristic resolved during discovery.
 */
struct ResolvedChar {
    Ble::UUID uuid;          /**< Characteristic UUID. */
    uint16_t declHandle;     /**< Declaration handle. */
    uint16_t valueHandle;    /**< Value handle. */
    uint16_t cccdHandle;     /**< Client Characteristic Config descriptor handle. */
    uint8_t props;           /**< Characteristic properties bitmask. */
    bool cccdFromDescriptor; /**< CCCD found by descriptor discovery (else the
                                  value+1 fallback - may not be a real CCCD). */
};

/**
 * @brief   A service and the subset of its characteristics that matched the
 *          client's requested UUIDs.
 */
struct ResolvedService {
    static constexpr uint8_t kMaxChars = 6;

    uint16_t startHandle;            /**< Service start (declaration) handle. */
    uint16_t endHandle;              /**< Service group-end handle. */
    uint8_t charCount;               /**< Number of valid entries in 'chars'. */
    ResolvedChar chars[kMaxChars];   /**< Matched characteristics. */
};

/**
 * @brief   A discoverable service client on a BLE link.
 */
class IGattClient {
public:

    /**
     * @brief   The primary service UUID this client looks for.
     */
    virtual const Ble::UUID& serviceUuid() const = 0;

    /**
     * @brief   Number of characteristic UUIDs of interest.
     */
    virtual uint8_t charCount() const = 0;

    /**
     * @brief   A characteristic UUID of interest.
     * @param   index: zero-based index, less than charCount().
     */
    virtual const Ble::UUID& charUuid(uint8_t index) const = 0;

    /**
     * @brief   The service was found and its characteristics resolved.
     * @param   res: resolved service and matched characteristic handles.
     * @note    The client performs its own subscription / setup writes here and
     *          must signal completion so the coordinator can proceed to the next
     *          client.
     */
    virtual void onServicesResolved(const ResolvedService& res) = 0;

    /**
     * @brief   The service or its required characteristics were not found.
     * @note    The coordinator proceeds to the next client automatically.
     */
    virtual void onServiceUnavailable() = 0;

    /**
     * @brief   A notification arrived on one of this client's value handles.
     * @param   attrHandle: notifying value handle.
     * @param   data: notification payload.
     * @param   len: payload length in bytes.
     */
    virtual void onNotification(uint16_t attrHandle, const uint8_t* data,
            uint8_t len) = 0;

    /**
     * @brief   The link dropped; discard any cached connection state.
     */
    virtual void onLinkDown() = 0;

    /**
     * @brief   Discovery of every link client has finished; the link is fully
     *          set up and runtime writes no longer collide with discovery.
     * @note    Optional hook (default no-op). A client whose consumer defers
     *          runtime requests until the shared link is usable can signal it
     *          here.
     */
    virtual void onLinkReady() {}

protected:
    virtual ~IGattClient() = default;
};

} // namespace Ble

#endif // BLE_DISCOVERY_IGATT_CLIENT_HPP
