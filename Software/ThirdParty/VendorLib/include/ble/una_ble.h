/**
 ******************************************************************************
 * @file    una_ble.h
 * @brief   Host interface to the BlueNRG-2 BLE network coprocessor.
 *
 *          Each command function sends the ACI or HCI command of the same name
 *          in ST's BlueNRG-1/2 ACI command reference: una_ble_X sends aci_X,
 *          and una_ble_hci_X sends hci_X, with the same parameters. Each
 *          una_ble_..._event() function is called with the event of the same
 *          name, and the firmware must provide it. Constants are named the same
 *          way, with a UNA_BLE_ prefix.
 ******************************************************************************
 */

#ifndef UNA_BLE_H
#define UNA_BLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Types -------------------------------------------------------------------- */

/// Result of a command; UNA_BLE_STATUS_SUCCESS or a Bluetooth/ACI error code
typedef uint8_t una_ble_status;

/// A 16-bit or 128-bit UUID, chosen by a separate UUID-type parameter
typedef union {
    uint16_t uuid_16;
    uint8_t  uuid_128[16];
} una_ble_uuid;

/// A device address and its type
typedef struct {
    uint8_t address_type;
    uint8_t address[6];
} una_ble_device_address;

/// One report in an advertising report event
typedef struct {
    uint8_t  event_type;
    uint8_t  address_type;
    uint8_t  address[6];
    uint8_t  data_length;
    uint8_t *data;
    int8_t   rssi;
} una_ble_adv_report;

/// One attribute group found by a find-by-type-value procedure
typedef struct {
    uint16_t found_attribute_handle;
    uint16_t group_end_handle;
} una_ble_handle_pair;

/// One attribute handle in a read-multiple permit request
typedef struct {
    uint16_t handle;
} una_ble_handle_item;

/**
 * The functions the host interface uses to reach the coprocessor over SPI.
 * Each returns 0 or a byte count on success, and a negative value on failure.
 */
typedef struct {
    int32_t (*init)(void *conf);
    int32_t (*deinit)(void);
    int32_t (*reset)(void);
    int32_t (*receive)(uint8_t *buffer, uint16_t size);
    int32_t (*send)(uint8_t *buffer, uint16_t size);
    int32_t (*get_tick)(void);              ///< Milliseconds since an arbitrary epoch
    int32_t (*delay)(uint32_t ms);
} una_ble_hci_io;

/* Constants ---------------------------------------------------------------- */

#define UNA_BLE_STATUS_SUCCESS                          0x00
#define UNA_BLE_STATUS_FAILED                           0x41
#define UNA_BLE_STATUS_INSUFFICIENT_RESOURCES           0x64

// GAP roles, for una_ble_gap_init()
#define UNA_BLE_GAP_PERIPHERAL_ROLE                     0x01
#define UNA_BLE_GAP_CENTRAL_ROLE                        0x04

// GAP procedure codes
#define UNA_BLE_GAP_GENERAL_DISCOVERY_PROC              0x02
#define UNA_BLE_GAP_DIRECT_CONNECTION_ESTABLISHMENT_PROC 0x40

// Advertising
#define UNA_BLE_ADV_IND                                 0x00
#define UNA_BLE_AD_TYPE_COMPLETE_LOCAL_NAME             0x09
#define UNA_BLE_AD_TYPE_TX_POWER_LEVEL                  0x0A
#define UNA_BLE_AD_TYPE_SLAVE_CONN_INTERVAL             0x12
#define UNA_BLE_AD_TYPE_MANUFACTURER_SPECIFIC_DATA      0xFF

// Filter policies
#define UNA_BLE_NO_WHITE_LIST_USE                       0x00
#define UNA_BLE_WHITE_LIST_FOR_ALL                      0x03

// Own address types
#define UNA_BLE_RESOLVABLE_PRIVATE_ADDR                 0x02

// Security
#define UNA_BLE_IO_CAP_DISPLAY_ONLY                     0x00
#define UNA_BLE_BONDING                                 0x01
#define UNA_BLE_MITM_PROTECTION_REQUIRED                0x01
#define UNA_BLE_SC_IS_MANDATORY                         0x02
#define UNA_BLE_KEYPRESS_IS_NOT_SUPPORTED               0x00
#define UNA_BLE_DONOT_USE_FIXED_PIN_FOR_PAIRING         0x01

// GATT server
#define UNA_BLE_UUID_TYPE_16                            0x01
#define UNA_BLE_UUID_TYPE_128                           0x02
#define UNA_BLE_PRIMARY_SERVICE                         0x01
#define UNA_BLE_ATTR_PERMISSION_ENCRY_READ              0x04
#define UNA_BLE_ATTR_PERMISSION_ENCRY_WRITE             0x20
#define UNA_BLE_GATT_DONT_NOTIFY_EVENTS                 0x00
#define UNA_BLE_GATT_NOTIFY_ATTRIBUTE_WRITE             0x01
#define UNA_BLE_GATT_NOTIFY_READ_REQ_AND_WAIT_FOR_APPL_RESP 0x04

/* Host interface ----------------------------------------------------------- */

/// Start the host interface. Events are delivered by una_ble_hci_user_evt_proc().
void una_ble_hci_init(void);

/// Set the SPI functions; may be called after una_ble_hci_init()
void una_ble_hci_register_io_bus(const una_ble_hci_io *io);

/**
 * Read one packet from the coprocessor; call while its IRQ line is high.
 * @return  0 on success, 1 if the receive queue is full
 */
int32_t una_ble_hci_notify_asynch_evt(void *data);

/// Deliver the received events, by calling the una_ble_..._event() functions
void una_ble_hci_user_evt_proc(void);

/* GAP commands ------------------------------------------------------------- */

una_ble_status una_ble_gap_init(uint8_t role, uint8_t privacy_enabled, uint8_t device_name_char_len,
                                uint16_t *service_handle, uint16_t *dev_name_char_handle,
                                uint16_t *appearance_char_handle);
una_ble_status una_ble_gap_set_io_capability(uint8_t io_capability);
una_ble_status una_ble_gap_set_authentication_requirement(uint8_t bonding_mode, uint8_t mitm_mode,
                                uint8_t sc_support, uint8_t keypress_notification_support,
                                uint8_t min_encryption_key_size, uint8_t max_encryption_key_size,
                                uint8_t use_fixed_pin, uint32_t fixed_pin, uint8_t identity_address_type);
una_ble_status una_ble_gap_set_discoverable(uint8_t advertising_type, uint16_t advertising_interval_min,
                                uint16_t advertising_interval_max, uint8_t own_address_type,
                                uint8_t advertising_filter_policy, uint8_t local_name_length,
                                uint8_t local_name[], uint8_t service_uuid_length,
                                uint8_t service_uuid_list[], uint16_t slave_conn_interval_min,
                                uint16_t slave_conn_interval_max);
una_ble_status una_ble_gap_set_undirected_connectable(uint16_t advertising_interval_min,
                                uint16_t advertising_interval_max, uint8_t own_address_type,
                                uint8_t adv_filter_policy);
una_ble_status una_ble_gap_set_non_discoverable(void);
una_ble_status una_ble_gap_delete_ad_type(uint8_t ad_type);
una_ble_status una_ble_gap_configure_whitelist(void);
una_ble_status una_ble_gap_add_devices_to_resolving_list(uint8_t num_entries,
                                una_ble_device_address entries[], uint8_t clear_resolving_list);
una_ble_status una_ble_gap_get_bonded_devices(uint8_t *num_addresses, una_ble_device_address devices[]);
una_ble_status una_ble_gap_clear_security_db(void);
una_ble_status una_ble_gap_allow_rebond(uint16_t connection_handle);
una_ble_status una_ble_gap_pass_key_resp(uint16_t connection_handle, uint32_t pass_key);
una_ble_status una_ble_gap_slave_security_req(uint16_t connection_handle);
una_ble_status una_ble_gap_terminate(uint16_t connection_handle, uint8_t reason);
una_ble_status una_ble_gap_start_connection_update(uint16_t connection_handle, uint16_t conn_interval_min,
                                uint16_t conn_interval_max, uint16_t conn_latency,
                                uint16_t supervision_timeout, uint16_t minimum_ce_length,
                                uint16_t maximum_ce_length);
una_ble_status una_ble_gap_start_general_discovery_proc(uint16_t le_scan_interval, uint16_t le_scan_window,
                                uint8_t own_address_type, uint8_t filter_duplicates);
una_ble_status una_ble_gap_create_connection(uint16_t le_scan_interval, uint16_t le_scan_window,
                                uint8_t peer_address_type, uint8_t peer_address[6],
                                uint8_t own_address_type, uint16_t conn_interval_min,
                                uint16_t conn_interval_max, uint16_t conn_latency,
                                uint16_t supervision_timeout, uint16_t minimum_ce_length,
                                uint16_t maximum_ce_length);
una_ble_status una_ble_gap_terminate_gap_proc(uint8_t procedure_code);

/* GATT commands ------------------------------------------------------------ */

una_ble_status una_ble_gatt_init(void);
una_ble_status una_ble_gatt_add_service(uint8_t service_uuid_type, una_ble_uuid *service_uuid,
                                uint8_t service_type, uint8_t max_attribute_records,
                                uint16_t *service_handle);
una_ble_status una_ble_gatt_add_char(uint16_t service_handle, uint8_t char_uuid_type,
                                una_ble_uuid *char_uuid, uint16_t char_value_length,
                                uint8_t char_properties, uint8_t security_permissions,
                                uint8_t gatt_evt_mask, uint8_t enc_key_size, uint8_t is_variable,
                                uint16_t *char_handle);
una_ble_status una_ble_gatt_update_char_value(uint16_t service_handle, uint16_t char_handle,
                                uint8_t val_offset, uint8_t char_value_length, uint8_t char_value[]);
una_ble_status una_ble_gatt_allow_read(uint16_t connection_handle);
una_ble_status una_ble_gatt_confirm_indication(uint16_t connection_handle);
una_ble_status una_ble_gatt_exchange_config(uint16_t connection_handle);
una_ble_status una_ble_gatt_disc_primary_service_by_uuid(uint16_t connection_handle, uint8_t uuid_type,
                                una_ble_uuid *uuid);
una_ble_status una_ble_gatt_disc_all_char_of_service(uint16_t connection_handle, uint16_t start_handle,
                                uint16_t end_handle);
una_ble_status una_ble_gatt_disc_char_by_uuid(uint16_t connection_handle, uint16_t start_handle,
                                uint16_t end_handle, uint8_t uuid_type, una_ble_uuid *uuid);
una_ble_status una_ble_gatt_disc_all_char_desc(uint16_t connection_handle, uint16_t char_handle,
                                uint16_t end_handle);
una_ble_status una_ble_gatt_write_char_value(uint16_t connection_handle, uint16_t attr_handle,
                                uint8_t attribute_val_length, uint8_t attribute_val[]);
una_ble_status una_ble_gatt_write_char_desc(uint16_t connection_handle, uint16_t attr_handle,
                                uint8_t attribute_val_length, uint8_t attribute_val[]);

/* L2CAP and HAL commands ---------------------------------------------------- */

una_ble_status una_ble_l2cap_connection_parameter_update_resp(uint16_t connection_handle,
                                uint16_t conn_interval_min, uint16_t conn_interval_max,
                                uint16_t slave_latency, uint16_t timeout_multiplier,
                                uint16_t minimum_ce_length, uint16_t maximum_ce_length,
                                uint8_t identifier, uint8_t accept);
una_ble_status una_ble_hal_get_firmware_details(uint8_t *dtm_version_major, uint8_t *dtm_version_minor,
                                uint8_t *dtm_version_patch, uint8_t *dtm_variant,
                                uint16_t *dtm_build_number, uint8_t *stack_version_major,
                                uint8_t *stack_version_minor, uint8_t *stack_version_patch,
                                uint8_t *stack_development, uint16_t *stack_variant,
                                uint16_t *stack_build_number);
una_ble_status una_ble_hal_read_config_data(uint8_t offset, uint8_t *data_length, uint8_t data[]);
una_ble_status una_ble_hal_set_tx_power_level(uint8_t en_high_power, uint8_t pa_level);
una_ble_status una_ble_hal_le_tx_test_packet_number(uint32_t *number_of_packets);

/* HCI commands ------------------------------------------------------------- */

una_ble_status una_ble_hci_read_local_version_information(uint8_t *hci_version, uint16_t *hci_revision,
                                uint8_t *lmp_pal_version, uint16_t *manufacturer_name,
                                uint16_t *lmp_pal_subversion);
una_ble_status una_ble_hci_le_rand(uint8_t random_number[8]);
una_ble_status una_ble_hci_le_set_scan_response_data(uint8_t scan_response_data_length,
                                uint8_t scan_response_data[31]);
una_ble_status una_ble_hci_le_set_data_length(uint16_t connection_handle, uint16_t tx_octets,
                                uint16_t tx_time);
una_ble_status una_ble_hci_le_write_suggested_default_data_length(uint16_t suggested_max_tx_octets,
                                uint16_t suggested_max_tx_time);
una_ble_status una_ble_hci_le_receiver_test(uint8_t rx_frequency);
una_ble_status una_ble_hci_le_transmitter_test(uint8_t tx_frequency, uint8_t length_of_test_data,
                                uint8_t packet_payload);
una_ble_status una_ble_hci_le_test_end(uint16_t *number_of_packets);

/* Events, provided by the firmware ------------------------------------------ */

void una_ble_blue_initialized_event(uint8_t reason_code);
void una_ble_blue_events_lost_event(uint8_t lost_events[8]);

void una_ble_hci_disconnection_complete_event(uint8_t status, uint16_t connection_handle, uint8_t reason);
void una_ble_hci_encryption_change_event(uint8_t status, uint16_t connection_handle,
                                uint8_t encryption_enabled);
void una_ble_hci_encryption_key_refresh_complete_event(uint8_t status, uint16_t connection_handle);
void una_ble_hci_le_connection_complete_event(uint8_t status, uint16_t connection_handle, uint8_t role,
                                uint8_t peer_address_type, uint8_t peer_address[6],
                                uint16_t conn_interval, uint16_t conn_latency,
                                uint16_t supervision_timeout, uint8_t master_clock_accuracy);
void una_ble_hci_le_enhanced_connection_complete_event(uint8_t status, uint16_t connection_handle,
                                uint8_t role, uint8_t peer_address_type, uint8_t peer_address[6],
                                uint8_t local_resolvable_private_address[6],
                                uint8_t peer_resolvable_private_address[6], uint16_t conn_interval,
                                uint16_t conn_latency, uint16_t supervision_timeout,
                                uint8_t master_clock_accuracy);
void una_ble_hci_le_connection_update_complete_event(uint8_t status, uint16_t connection_handle,
                                uint16_t conn_interval, uint16_t conn_latency,
                                uint16_t supervision_timeout);
void una_ble_hci_le_data_length_change_event(uint16_t connection_handle, uint16_t max_tx_octets,
                                uint16_t max_tx_time, uint16_t max_rx_octets, uint16_t max_rx_time);
void una_ble_hci_le_advertising_report_event(uint8_t num_reports, una_ble_adv_report reports[]);

void una_ble_gap_pairing_complete_event(uint16_t connection_handle, uint8_t status, uint8_t reason);
void una_ble_gap_pass_key_req_event(uint16_t connection_handle);
void una_ble_gap_slave_security_initiated_event(void);
void una_ble_gap_bond_lost_event(void);
void una_ble_gap_addr_not_resolved_event(uint16_t connection_handle);
void una_ble_gap_proc_complete_event(uint8_t procedure_code, uint8_t status, uint8_t data_length,
                                uint8_t data[]);

void una_ble_l2cap_connection_update_req_event(uint16_t connection_handle, uint8_t identifier,
                                uint16_t l2cap_length, uint16_t interval_min, uint16_t interval_max,
                                uint16_t slave_latency, uint16_t timeout_multiplier);

void una_ble_gatt_attribute_modified_event(uint16_t connection_handle, uint16_t attr_handle,
                                uint16_t offset, uint16_t attr_data_length, uint8_t attr_data[]);
void una_ble_gatt_read_permit_req_event(uint16_t connection_handle, uint16_t attribute_handle,
                                uint16_t offset);
void una_ble_gatt_read_multi_permit_req_event(uint16_t connection_handle, uint8_t number_of_handles,
                                una_ble_handle_item handles[]);
void una_ble_gatt_server_confirmation_event(uint16_t connection_handle);
void una_ble_gatt_tx_pool_available_event(uint16_t connection_handle, uint16_t available_buffers);
void una_ble_gatt_proc_timeout_event(uint16_t connection_handle);
void una_ble_gatt_proc_complete_event(uint16_t connection_handle, uint8_t error_code);
void una_ble_gatt_error_resp_event(uint16_t connection_handle, uint8_t req_opcode,
                                uint16_t attribute_handle, uint8_t error_code);
void una_ble_gatt_notification_event(uint16_t connection_handle, uint16_t attribute_handle,
                                uint8_t attribute_value_length, uint8_t attribute_value[]);
void una_ble_gatt_indication_event(uint16_t connection_handle, uint16_t attribute_handle,
                                uint8_t attribute_value_length, uint8_t attribute_value[]);
void una_ble_gatt_disc_read_char_by_uuid_resp_event(uint16_t connection_handle,
                                uint16_t attribute_handle, uint8_t attribute_value_length,
                                uint8_t attribute_value[]);

void una_ble_att_exchange_mtu_resp_event(uint16_t connection_handle, uint16_t server_rx_mtu);
void una_ble_att_find_by_type_value_resp_event(uint16_t connection_handle, uint8_t num_of_handle_pair,
                                una_ble_handle_pair pairs[]);
void una_ble_att_read_by_type_resp_event(uint16_t connection_handle, uint8_t handle_value_pair_length,
                                uint8_t data_length, uint8_t handle_value_pair_data[]);
void una_ble_att_find_info_resp_event(uint16_t connection_handle, uint8_t format,
                                uint8_t event_data_length, uint8_t handle_uuid_pair[]);

#ifdef __cplusplus
}
#endif

#endif /* UNA_BLE_H */
