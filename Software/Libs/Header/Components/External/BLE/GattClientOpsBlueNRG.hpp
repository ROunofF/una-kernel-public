/**
 ******************************************************************************
 * @file    GattClientOpsBlueNRG.hpp
 * @brief   BlueNRG-2 implementation of the GATT-client operations port.
 ******************************************************************************
 *
 * The single place where phone-link GATT-client discovery touches the BlueNRG-2
 * stack: it wraps the ACI discovery and write commands and serialises them on
 * the core mutex. Everything above it (the coordinator and the service clients)
 * works against the IGattClientOps abstraction.
 *
 ******************************************************************************
 */

#ifndef BLE_GATT_CLIENT_OPS_BLUENRG_HPP
#define BLE_GATT_CLIENT_OPS_BLUENRG_HPP

#include "IGattClientOps.hpp"
#include "CoreBlueNRG.hpp"

namespace Ble
{

/**
 * @brief   BlueNRG-2 backed outbound GATT-client operations.
 */
class GattClientOpsBlueNRG : public IGattClientOps {
public:

    /**
     * @brief   Construct bound to the BLE core.
     * @param   core: core used to serialise ACI access.
     */
    explicit GattClientOpsBlueNRG(CoreBlueNRG& core) : mCore(core) {}

    bool discoverPrimaryServiceByUuid(uint16_t connHandle,
            const Ble::UUID& uuid) override;
    bool discoverAllChars(uint16_t connHandle, uint16_t startHandle,
            uint16_t endHandle) override;
    bool discoverAllCharDescriptors(uint16_t connHandle, uint16_t startHandle,
            uint16_t endHandle) override;
    bool writeCharValue(uint16_t connHandle, uint16_t valueHandle,
            const uint8_t* data, uint16_t len) override;
    bool writeCharDescriptor(uint16_t connHandle, uint16_t descHandle,
            const uint8_t* data, uint16_t len) override;

private:
    CoreBlueNRG& mCore;
};

} // namespace Ble

#endif // BLE_GATT_CLIENT_OPS_BLUENRG_HPP
