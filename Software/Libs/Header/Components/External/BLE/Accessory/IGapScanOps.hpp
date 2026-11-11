/**
 ******************************************************************************
 * @file    IGapScanOps.hpp
 * @brief   GAP discovery-scan operations port.
 ******************************************************************************
 *
 * The GAP scan operations the accessory scanner needs, with no controller-stack
 * types in the signature. Mirrors IGattClientOps: confining the stack-specific
 * calls to one implementation lets the scanning LOGIC -- which UUIDs are wanted,
 * whether a report matches, whether a scan is already running -- be exercised by
 * host unit tests against a mock port.
 *
 * GAP discovery is a GLOBAL, SINGLE-INSTANCE radio resource: only one discovery
 * procedure can run at a time, and it must be terminated before a connection is
 * initiated. That constraint is why AccessoryScanner exists as one object rather
 * than something each link owns, and it is the reason this port is deliberately
 * narrow -- there is nothing here to let two callers scan at once.
 *
 ******************************************************************************
 */

#ifndef BLE_IGAP_SCAN_OPS_HPP
#define BLE_IGAP_SCAN_OPS_HPP

#include <cstdint>

namespace Ble
{

/**
 * @brief   GAP general-discovery operations.
 */
class IGapScanOps {
public:

    /**
     * @brief   Start a general discovery (scan) procedure.
     * @param   intervalUnits: scan interval in 0.625 ms units.
     * @param   windowUnits: scan window in 0.625 ms units.
     * @param   filterDuplicates: 'true' to have the controller suppress repeat
     *          reports. Keep this ON: a flood of duplicates backs the controller
     *          event queue up into aci_blue_events_lost.
     * @retval  'true' if the controller accepted the procedure.
     */
    virtual bool startGeneralDiscovery(uint16_t intervalUnits,
            uint16_t windowUnits, bool filterDuplicates) = 0;

    /**
     * @brief   Terminate the running general-discovery procedure.
     * @retval  'true' if the controller accepted the termination.
     */
    virtual bool terminateGeneralDiscovery() = 0;

protected:
    virtual ~IGapScanOps() = default;
};

} // namespace Ble

#endif // BLE_IGAP_SCAN_OPS_HPP
