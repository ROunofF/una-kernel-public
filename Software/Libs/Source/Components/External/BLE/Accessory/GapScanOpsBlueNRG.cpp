/**
 ******************************************************************************
 * @file    GapScanOpsBlueNRG.cpp
 * @brief   BlueNRG-2 implementation of the GAP scan-operations port.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.ScanOps"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "GapScanOpsBlueNRG.hpp"

#include "una_ble.h"

namespace Ble
{

namespace {

// GAP is initialised with controller privacy enabled, so only a resolvable
// private own-address is accepted; public/random are rejected 0x42.
constexpr uint8_t kOwnAddrType = UNA_BLE_RESOLVABLE_PRIVATE_ADDR;

}  // namespace

bool GapScanOpsBlueNRG::startGeneralDiscovery(uint16_t intervalUnits,
        uint16_t windowUnits, bool filterDuplicates)
{
    mCore.lock();
    const una_ble_status status = una_ble_gap_start_general_discovery_proc(
            intervalUnits, windowUnits, kOwnAddrType,
            filterDuplicates ? 0x01 : 0x00);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        // 0xFF blocks for the whole HCI timeout while holding the core mutex,
        // starving the event pump -- the caller's back-off exists for exactly
        // this, so surface the code and let it decide.
        LOG_ERROR("scan-start failed [0x%02X]\n", status);
        return false;
    }
    return true;
}

bool GapScanOpsBlueNRG::terminateGeneralDiscovery()
{
    mCore.lock();
    const una_ble_status status =
            una_ble_gap_terminate_gap_proc(UNA_BLE_GAP_GENERAL_DISCOVERY_PROC);
    mCore.release();

    if (status != UNA_BLE_STATUS_SUCCESS) {
        LOG_WARNING("scan-terminate refused [0x%02X]\n", status);
        return false;
    }
    return true;
}

}  /* namespace Ble */
