/**
 ******************************************************************************
 * @file    GattClientOpsBlueNRG.cpp
 * @brief   BlueNRG-2 implementation of the GATT-client operations port.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.GattOps"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "GattClientOpsBlueNRG.hpp"

#include <cstring>

#include "una_ble.h"

namespace Ble
{

///< ACI UUID-type selector for a 128-bit UUID.
static constexpr uint8_t kUuidType128 = 0x02;

bool GattClientOpsBlueNRG::discoverPrimaryServiceByUuid(uint16_t connHandle,
        const Ble::UUID& uuid)
{
    una_ble_uuid u { };
    memcpy(u.uuid_128, uuid.get128(), 16);

    mCore.lock();
    una_ble_status status = una_ble_gatt_disc_primary_service_by_uuid(connHandle,
            kUuidType128, &u);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_DEBUG("una_ble_gatt_disc_primary_service_by_uuid() failed [0x%02X]\n",
                status);
    }
    return status == UNA_BLE_STATUS_SUCCESS;
}

bool GattClientOpsBlueNRG::discoverAllChars(uint16_t connHandle,
        uint16_t startHandle, uint16_t endHandle)
{
    mCore.lock();
    una_ble_status status = una_ble_gatt_disc_all_char_of_service(connHandle,
            startHandle, endHandle);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_DEBUG("una_ble_gatt_disc_all_char_of_service() failed [0x%02X]\n",
                status);
    }
    return status == UNA_BLE_STATUS_SUCCESS;
}

bool GattClientOpsBlueNRG::discoverAllCharDescriptors(uint16_t connHandle,
        uint16_t startHandle, uint16_t endHandle)
{
    mCore.lock();
    una_ble_status status = una_ble_gatt_disc_all_char_desc(connHandle, startHandle,
            endHandle);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_DEBUG("una_ble_gatt_disc_all_char_desc() failed [0x%02X]\n", status);
    }
    return status == UNA_BLE_STATUS_SUCCESS;
}

bool GattClientOpsBlueNRG::writeCharValue(uint16_t connHandle,
        uint16_t valueHandle, const uint8_t* data, uint16_t len)
{
    mCore.lock();
    una_ble_status status = una_ble_gatt_write_char_value(connHandle, valueHandle, len,
            (uint8_t*) data);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_DEBUG("una_ble_gatt_write_char_value() failed [0x%02X]\n", status);
    }
    return status == UNA_BLE_STATUS_SUCCESS;
}

bool GattClientOpsBlueNRG::writeCharDescriptor(uint16_t connHandle,
        uint16_t descHandle, const uint8_t* data, uint16_t len)
{
    mCore.lock();
    una_ble_status status = una_ble_gatt_write_char_desc(connHandle, descHandle, len,
            (uint8_t*) data);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_DEBUG("una_ble_gatt_write_char_desc() failed [0x%02X]\n", status);
    }
    return status == UNA_BLE_STATUS_SUCCESS;
}

} // namespace Ble
