/**
 ******************************************************************************
 * @file    BleEventDispatch.cpp
 * @brief   Global BlueNRG C event handlers that steer per-link events through
 *          Ble::EventRouter to the owning Ble::ILinkClient.
 ******************************************************************************
 *
 * The BlueNRG-2 stack overrides a fixed set of weak C callbacks. The handlers
 * here own the symbols that must be routed by connection handle (or, for
 * connection-complete, by role) so that the phone peripheral link and any
 * accessory central link reach their respective owners. Server-side and
 * connection-tuning events that only ever concern the phone link stay in
 * PeripheralBlueNRG.cpp.
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.Dispatch"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cstring>

#include "OS.hpp"
#include "EventRouter.hpp"
#include "una_ble.h"

extern "C" {

void una_ble_hci_le_connection_complete_event(uint8_t Status,
        uint16_t Connection_Handle, uint8_t Role, uint8_t Peer_Address_Type,
        uint8_t Peer_Address[6], uint16_t Conn_Interval, uint16_t Conn_Latency,
        uint16_t Supervision_Timeout, uint8_t Master_Clock_Accuracy)
{
    (void)Master_Clock_Accuracy;
    LOG_DEBUG("Status 0x%02X Handle 0x%04X Role 0x%02X Interval 0x%04X Latency 0x%04X Timeout 0x%04X\n",
            Status, Connection_Handle, Role, Conn_Interval, Conn_Latency,
            Supervision_Timeout);

    if (Status == UNA_BLE_STATUS_SUCCESS) {
        LOG_INFO("Conn-complete (legacy) role=0x%02X handle=0x%04X\n",
                Role, Connection_Handle);
        if (!Ble::bleEventRouter().onConnectionComplete(
                static_cast<Ble::LinkRole>(Role), Connection_Handle,
                Peer_Address_Type, Peer_Address, Conn_Interval, Conn_Latency,
                Supervision_Timeout)) {
            LOG_WARNING("conn 0x%04X not routed (role 0x%02X): no owner or "
                    "link table full\n", Connection_Handle, Role);
        }
    } else {
        LOG_WARNING("Connection failed.  [0x%02X]", Status);
    }
}

void una_ble_hci_le_enhanced_connection_complete_event(uint8_t Status,
        uint16_t Connection_Handle, uint8_t Role, uint8_t Peer_Address_Type,
        uint8_t Peer_Address[6], uint8_t Local_Resolvable_Private_Address[6],
        uint8_t Peer_Resolvable_Private_Address[6], uint16_t Conn_Interval,
        uint16_t Conn_Latency, uint16_t Supervision_Timeout,
        uint8_t Master_Clock_Accuracy)
{
    (void)Local_Resolvable_Private_Address;
    (void)Peer_Resolvable_Private_Address;
    (void)Master_Clock_Accuracy;
    LOG_DEBUG("Status 0x%02X Handle 0x%04X Role 0x%02X Interval 0x%04X Latency 0x%04X Timeout 0x%04X\n",
            Status, Connection_Handle, Role, Conn_Interval, Conn_Latency,
            Supervision_Timeout);

    // Privacy-enabled links surface through the enhanced event; route it the
    // same way as the legacy connection-complete.
    if (Status == UNA_BLE_STATUS_SUCCESS) {
        LOG_INFO("Conn-complete (enhanced) role=0x%02X handle=0x%04X\n",
                Role, Connection_Handle);
        if (!Ble::bleEventRouter().onConnectionComplete(
                static_cast<Ble::LinkRole>(Role), Connection_Handle,
                Peer_Address_Type, Peer_Address, Conn_Interval, Conn_Latency,
                Supervision_Timeout)) {
            LOG_WARNING("conn 0x%04X not routed (role 0x%02X): no owner or "
                    "link table full\n", Connection_Handle, Role);
        }
    } else {
        LOG_WARNING("Connection failed.  [0x%02X]", Status);
    }
}

/**
 * @brief   The peer requests different connection parameters (central role).
 * @note    Routed rather than answered here: the response is an ACI command and
 *          this runs on the event thread with CoreBlueNRG's mutex already held,
 *          so the owner defers it onto the BLE work queue.
 */
void una_ble_l2cap_connection_update_req_event(uint16_t Connection_Handle,
        uint8_t Identifier, uint16_t L2CAP_Length, uint16_t Interval_Min,
        uint16_t Interval_Max, uint16_t Slave_Latency,
        uint16_t Timeout_Multiplier)
{
    (void)L2CAP_Length;
    LOG_INFO("L2CAP conn-param REQ h=0x%04X interval %u-%u (%u-%u ms) "
             "latency %u timeout %u (%u ms)\n",
            Connection_Handle,
            (unsigned)Interval_Min, (unsigned)Interval_Max,
            (unsigned)((Interval_Min * 5u) / 4u),
            (unsigned)((Interval_Max * 5u) / 4u),
            (unsigned)Slave_Latency,
            (unsigned)Timeout_Multiplier,
            (unsigned)(Timeout_Multiplier * 10u));

    Ble::bleEventRouter().onL2capUpdateReq(Connection_Handle, Identifier,
            Interval_Min, Interval_Max, Slave_Latency, Timeout_Multiplier);
}

void una_ble_hci_disconnection_complete_event(uint8_t Status,
        uint16_t Connection_Handle, uint8_t Reason)
{
    (void)Status;

    // Controller settle window after a disconnection (see @file
    // bluenrg1_gap_aci.h, una_ble_gap_terminate()). Applies to every link, not just
    // the phone — deliberate: an accessory/central disconnect needs the same
    // settle before the next GAP op. It runs on the event thread, before
    // routing, so the owner's onDisconnected() sees a settled controller.
    OS::Delay(100);

    // If this line appears without the owner's own "Disconnected" log following
    // it, the handle was never bound — a routing miss worth investigating.
    LOG_INFO("Disconnect handle=0x%04X reason=0x%02X\n", Connection_Handle,
            Reason);
    Ble::bleEventRouter().onDisconnection(Connection_Handle, Reason);
}

void una_ble_att_find_by_type_value_resp_event(uint16_t Connection_Handle,
        uint8_t Num_of_Handle_Pair,
        una_ble_handle_pair Attribute_Group_Handle_Pair[])
{
    LOG_DEBUG("Connection_Handle 0x%04X Num_of_Handle_Pair %d\n",
            Connection_Handle, Num_of_Handle_Pair);

    Ble::EventRouter &router = Ble::bleEventRouter();
    for (uint8_t i = 0; i < Num_of_Handle_Pair; i++) {
        router.onServiceGroupFound(Connection_Handle,
                Attribute_Group_Handle_Pair[i].found_attribute_handle,
                Attribute_Group_Handle_Pair[i].group_end_handle);
    }
}

void una_ble_att_read_by_type_resp_event(uint16_t Connection_Handle,
        uint8_t Handle_Value_Pair_Length, uint8_t Data_Length,
        uint8_t Handle_Value_Pair_Data[])
{
    LOG_DEBUG("Connection_Handle 0x%04X PairLen %d DataLen %d\n",
            Connection_Handle, Handle_Value_Pair_Length, Data_Length);
    LOG_DEBUG_DUMP(Handle_Value_Pair_Data, Data_Length);

    if (Handle_Value_Pair_Length == 0) {
        return;
    }

    Ble::EventRouter &router = Ble::bleEventRouter();
    uint32_t numAttr = Data_Length / Handle_Value_Pair_Length;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < numAttr; i++) {
        router.onCharDiscovered(Connection_Handle,
                &Handle_Value_Pair_Data[offset], Handle_Value_Pair_Length);
        offset += Handle_Value_Pair_Length;
    }
}

void una_ble_att_find_info_resp_event(uint16_t Connection_Handle, uint8_t Format,
        uint8_t Event_Data_Length, uint8_t Handle_UUID_Pair[])
{
    // Format 0x01 = 16-bit UUID descriptors (handle[2] + uuid[2]); 0x02 = 128-bit
    // (handle[2] + uuid[16]). Only 16-bit descriptors carry the CCCD (0x2902).
    if (Format != 0x01) {
        return;
    }

    Ble::EventRouter &router = Ble::bleEventRouter();
    const uint8_t pairLen = 4;
    uint32_t numPairs = Event_Data_Length / pairLen;
    uint32_t offset = 0;
    for (uint32_t i = 0; i < numPairs; i++) {
        uint16_t handle;
        uint16_t uuid16;
        memcpy(&handle, &Handle_UUID_Pair[offset], 2);
        memcpy(&uuid16, &Handle_UUID_Pair[offset + 2], 2);
        router.onCharDescFound(Connection_Handle, handle, uuid16);
        offset += pairLen;
    }
}

void una_ble_gatt_disc_read_char_by_uuid_resp_event(uint16_t Connection_Handle,
        uint16_t Attribute_Handle, uint8_t Attribute_Value_Length,
        uint8_t Attribute_Value[])
{
    LOG_DEBUG("Connection_Handle 0x%04X Attr 0x%04X Len %u\n",
            Connection_Handle, Attribute_Handle, Attribute_Value_Length);

    Ble::bleEventRouter().onCharByUuidResp(Connection_Handle, Attribute_Handle,
            Attribute_Value, Attribute_Value_Length);
}

void una_ble_gatt_notification_event(uint16_t Connection_Handle,
        uint16_t Attribute_Handle, uint8_t Attribute_Value_Length,
        uint8_t Attribute_Value[])
{
    LOG_DEBUG("Connection_Handle 0x%04X Attr 0x%04X Len 0x%02X\n",
            Connection_Handle, Attribute_Handle, Attribute_Value_Length);
    LOG_DEBUG_DUMP(Attribute_Value, Attribute_Value_Length);

    Ble::bleEventRouter().onNotification(Connection_Handle, Attribute_Handle,
            Attribute_Value, Attribute_Value_Length);
}

void una_ble_gatt_proc_complete_event(uint16_t Connection_Handle, uint8_t Error_Code)
{
    LOG_DEBUG("Connection_Handle 0x%04X, Error_Code 0x%02X\n",
            Connection_Handle, Error_Code);

    Ble::bleEventRouter().onGattProcComplete(Connection_Handle, Error_Code);
}

void una_ble_gatt_error_resp_event(uint16_t Connection_Handle, uint8_t Req_Opcode,
        uint16_t Attribute_Handle, uint8_t Error_Code)
{
    LOG_DEBUG("Connection_Handle 0x%04X Req_Opcode 0x%02X Attr 0x%04X Error 0x%02X\n",
            Connection_Handle, Req_Opcode, Attribute_Handle, Error_Code);

    Ble::bleEventRouter().onGattError(Connection_Handle, Req_Opcode,
            Attribute_Handle, Error_Code);
}

void una_ble_hci_le_advertising_report_event(uint8_t Num_Reports,
        una_ble_adv_report Advertising_Report[])
{
    // Only fires while a central scan is active; route each report to the
    // central-role client (AccessoryCentral).
    Ble::EventRouter &router = Ble::bleEventRouter();
    for (uint8_t i = 0; i < Num_Reports; i++) {
        const una_ble_adv_report &r = Advertising_Report[i];
        // Event_Type distinguishes UNA_BLE_ADV_IND from SCAN_RSP (0x04). It was
        // previously dropped here, which is why a peer advertising its Local
        // Name only in the scan response could never be named.
        router.onAdvReport(r.address_type, r.address, r.data_length, r.data,
                r.rssi, r.event_type);
    }
}

void una_ble_gap_proc_complete_event(uint8_t Procedure_Code, uint8_t Status,
        uint8_t Data_Length, uint8_t Data[])
{
    LOG_DEBUG("Procedure_Code 0x%02X Status 0x%02X Data_Length %u\n",
            Procedure_Code, Status, Data_Length);

    // No connection handle: this event only fires for central-initiated GAP
    // procedures (scan/connect/discovery), so the router delivers it to the
    // central-role client.
    Ble::bleEventRouter().onGapProcComplete(Procedure_Code, Status, Data,
            Data_Length);
}

} /* extern "C" */
