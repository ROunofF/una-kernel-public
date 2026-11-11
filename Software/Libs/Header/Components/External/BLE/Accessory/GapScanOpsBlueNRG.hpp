/**
 ******************************************************************************
 * @file    GapScanOpsBlueNRG.hpp
 * @brief   BlueNRG-2 implementation of the GAP scan-operations port.
 ******************************************************************************
 *
 * The single place where accessory scanning touches the BlueNRG-2 stack: wraps
 * the ACI general-discovery start/terminate commands and serialises them on the
 * core mutex. AccessoryScanner works against the IGapScanOps abstraction, so its
 * logic is host-testable. Peer of GattClientOpsBlueNRG.
 *
 ******************************************************************************
 */

#ifndef BLE_GAP_SCAN_OPS_BLUENRG_HPP
#define BLE_GAP_SCAN_OPS_BLUENRG_HPP

#include "IGapScanOps.hpp"
#include "CoreBlueNRG.hpp"

namespace Ble
{

/**
 * @brief   BlueNRG-2 backed GAP discovery-scan operations.
 */
class GapScanOpsBlueNRG : public IGapScanOps {
public:

    /**
     * @brief   Construct bound to the BLE core.
     * @param   core: core used to serialise ACI access.
     */
    explicit GapScanOpsBlueNRG(CoreBlueNRG &core) : mCore(core) {}

    bool startGeneralDiscovery(uint16_t intervalUnits, uint16_t windowUnits,
            bool filterDuplicates) override;
    bool terminateGeneralDiscovery() override;

private:
    CoreBlueNRG &mCore;
};

} // namespace Ble

#endif // BLE_GAP_SCAN_OPS_BLUENRG_HPP
