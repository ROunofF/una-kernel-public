/**
 ******************************************************************************
 * @file    ILinkClient.hpp
 * @brief   Narrow interface implemented by an owner of a single BLE link.
 ******************************************************************************
 *
 * A link client owns one logical BLE connection (the phone/peripheral link,
 * or a future accessory/central link) and receives the BlueNRG events that
 * have been routed to it by Ble::EventRouter. Keeping the interface free of
 * BlueNRG stack types lets the router be exercised by host unit tests.
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_ILINK_CLIENT_HPP
#define __COMPONENT_BLE_ILINK_CLIENT_HPP

#include <cstdint>

namespace Ble
{

/**
 * @brief   Owner of a single BLE link, fed by Ble::EventRouter.
 * @note    Every method has a no-op default so a client overrides only the
 *          events it cares about (e.g. the phone link ignores the central-only
 *          GAP-procedure event; a sensor profile ignores server-side writes).
 *          All callbacks run in the BlueNRG event-processing context.
 */
class ILinkClient {
public:

    /**
     * @brief   A connection on which this client is the owner has completed.
     * @param   connHandle: connection handle assigned by the controller.
     * @param   peerAddrType: address type of the peer.
     * @param   peerAddr: 6-byte peer address (identity address under privacy).
     * @param   interval: negotiated connection interval.
     * @param   latency: negotiated peripheral latency.
     * @param   supTimeout: negotiated supervision timeout.
     */
    virtual void onConnected(uint16_t connHandle, uint8_t peerAddrType,
            const uint8_t peerAddr[6], uint16_t interval, uint16_t latency,
            uint16_t supTimeout) { (void)connHandle; (void)peerAddrType;
            (void)peerAddr; (void)interval; (void)latency; (void)supTimeout; }

    /**
     * @brief   This client's connection has dropped.
     * @param   connHandle: connection handle that disconnected.
     * @param   reason: HCI disconnection reason code.
     */
    virtual void onDisconnected(uint16_t connHandle, uint8_t reason)
            { (void)connHandle; (void)reason; }

    /**
     * @brief   A primary-service-by-UUID discovery returned a group of handles
     *          (una_ble_att_find_by_type_value_resp_event), one call per pair.
     * @param   connHandle: connection handle.
     * @param   foundHandle: start (declaration) handle of the found service.
     * @param   endHandle: group-end handle of the found service.
     */
    virtual void onServiceGroupFound(uint16_t connHandle, uint16_t foundHandle,
            uint16_t endHandle) { (void)connHandle; (void)foundHandle;
            (void)endHandle; }

    /**
     * @brief   A characteristic was discovered while reading by type
     *          (una_ble_att_read_by_type_resp_event), one call per attribute.
     * @param   connHandle: connection handle.
     * @param   attrData: raw handle/value pair for the characteristic.
     * @param   len: length of attrData in bytes.
     */
    virtual void onCharDiscovered(uint16_t connHandle, const uint8_t *attrData,
            uint8_t len) { (void)connHandle; (void)attrData; (void)len; }

    /**
     * @brief   A characteristic descriptor was discovered
     *          (una_ble_att_find_info_resp_event), one call per 16-bit descriptor.
     * @param   connHandle: connection handle.
     * @param   attrHandle: descriptor handle.
     * @param   uuid16: 16-bit descriptor UUID (e.g. 0x2902 for a CCCD).
     */
    virtual void onCharDescFound(uint16_t connHandle, uint16_t attrHandle,
            uint16_t uuid16) { (void)connHandle; (void)attrHandle;
            (void)uuid16; }

    /**
     * @brief   A read-characteristic-by-UUID procedure returned a value
     *          (una_ble_gatt_disc_read_char_by_uuid_resp_event).
     * @param   connHandle: connection handle.
     * @param   attrHandle: attribute handle of the value read.
     * @param   value: characteristic value bytes.
     * @param   len: length of value in bytes.
     */
    virtual void onCharByUuidResp(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *value, uint8_t len) { (void)connHandle;
            (void)attrHandle; (void)value; (void)len; }

    /**
     * @brief   A GATT notification arrived (una_ble_gatt_notification_event).
     * @param   connHandle: connection handle.
     * @param   attrHandle: attribute handle the notification is for.
     * @param   data: notification payload.
     * @param   len: length of data in bytes.
     */
    virtual void onNotification(uint16_t connHandle, uint16_t attrHandle,
            const uint8_t *data, uint8_t len) { (void)connHandle;
            (void)attrHandle; (void)data; (void)len; }

    /**
     * @brief   A GATT client procedure completed (una_ble_gatt_proc_complete_event).
     * @param   connHandle: connection handle.
     * @param   errorCode: completion status (0 = success).
     */
    virtual void onGattProcComplete(uint16_t connHandle, uint8_t errorCode)
            { (void)connHandle; (void)errorCode; }

    /**
     * @brief   A GATT error response arrived (una_ble_gatt_error_resp_event).
     * @param   connHandle: connection handle.
     * @param   reqOpcode: opcode of the request that errored.
     * @param   attrHandle: attribute handle the error refers to.
     * @param   errorCode: ATT error code (e.g. 0x0A attribute-not-found).
     */
    virtual void onGattError(uint16_t connHandle, uint8_t reqOpcode,
            uint16_t attrHandle, uint8_t errorCode) { (void)connHandle;
            (void)reqOpcode; (void)attrHandle; (void)errorCode; }

    /**
     * @brief   A connection-parameter update completed on this link
     *          (una_ble_hci_le_connection_update_complete_event).
     * @param   connHandle: connection handle.
     * @param   status: 0 on success; 0x84 NEW_INTERVAL_FAILED / 0x85
     *          INTERVAL_TOO_LARGE are the anchor-slot rejections.
     * @param   interval: interval now in force (1.25 ms units).
     * @param   latency: peripheral latency now in force.
     * @param   supTimeout: supervision timeout now in force (10 ms units).
     * @note    This is the ONLY place a link learns whether a parameter change
     *          it requested actually happened. una_ble_gap_start_connection_update
     *          returning success means the command was accepted, not that the
     *          link-layer procedure succeeded -- so without this, a relax that
     *          fails at the link layer leaves the link at its connect-time
     *          interval with nothing noticing, which is precisely how a Polar
     *          H10 ends up going silent at 120 s.
     */
    virtual void onConnUpdateComplete(uint16_t connHandle, uint8_t status,
            uint16_t interval, uint16_t latency, uint16_t supTimeout)
            { (void)connHandle; (void)status; (void)interval; (void)latency;
              (void)supTimeout; }

    /**
     * @brief   The peer asked us to change the connection parameters
     *          (una_ble_l2cap_connection_update_req_event). Central role only: a
     *          peripheral requests, the central decides.
     * @param   connHandle: connection handle.
     * @param   identifier: L2CAP signalling identifier; MUST be echoed in the
     *          response or the peer cannot match it to its request.
     * @param   intervalMin: requested minimum connection interval (1.25 ms).
     * @param   intervalMax: requested maximum connection interval (1.25 ms).
     * @param   latency: requested peripheral latency, in connection events.
     * @param   supTimeout: requested supervision timeout (10 ms).
     * @note    A peer that is ignored may simply stop sending: the Polar H10
     *          streams for exactly 120 s, then goes quiet while holding the
     *          link up, with no disconnection and no error. Silence is a
     *          legitimate response for it to choose, so leaving this unhandled
     *          costs data, not connectivity -- which is what made it hard to
     *          find. Answer the request.
     */
    virtual void onL2capUpdateReq(uint16_t connHandle, uint8_t identifier,
            uint16_t intervalMin, uint16_t intervalMax, uint16_t latency,
            uint16_t supTimeout) { (void)connHandle; (void)identifier;
            (void)intervalMin; (void)intervalMax; (void)latency;
            (void)supTimeout; }

    /**
     * @brief   An advertising report seen during a central scan
     *          (una_ble_hci_le_advertising_report_event). No connection exists yet, so
     *          it is delivered to the central-role client.
     * @param   addrType: advertiser address type.
     * @param   addr: 6-byte advertiser address.
     * @param   dataLen: length of the advertising data.
     * @param   data: raw advertising data (AD structures).
     * @param   rssi: received signal strength, dBm.
     */
    virtual void onAdvReport(uint8_t addrType, const uint8_t addr[6],
            uint8_t dataLen, const uint8_t *data, int8_t rssi,
            uint8_t eventType)
            { (void)addrType; (void)addr; (void)dataLen; (void)data; (void)rssi;
              (void)eventType; }

    /**
     * @brief   A central-initiated GAP procedure completed
     *          (una_ble_gap_proc_complete_event). Carries no connection handle, so
     *          it is delivered to the central-role client.
     * @param   procedureCode: GAP procedure code that completed.
     * @param   status: completion status.
     * @param   data: procedure-specific data.
     * @param   len: length of data in bytes.
     */
    virtual void onGapProcComplete(uint8_t procedureCode, uint8_t status,
            const uint8_t *data, uint8_t len) { (void)procedureCode;
            (void)status; (void)data; (void)len; }

protected:
    virtual ~ILinkClient() = default;
};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_ILINK_CLIENT_HPP */
