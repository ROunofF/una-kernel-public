/**
 ******************************************************************************
 * @file    IGattClientOps.hpp
 * @brief   Outbound GATT-client operations port for a single BLE link.
 ******************************************************************************
 *
 * The outbound GATT operations a link exposes to its client side: the discovery
 * coordinator uses the service/characteristic/descriptor discovery calls, and
 * the service clients use the descriptor and value writes to subscribe and to
 * push commands. Keeping the port free of any controller-stack types confines
 * the stack-specific code to one implementation and lets the coordinator and its
 * clients be exercised by host unit tests with a mock port.
 *
 ******************************************************************************
 */

#ifndef BLE_IGATT_CLIENT_OPS_HPP
#define BLE_IGATT_CLIENT_OPS_HPP

#include <cstdint>

#include "IBle.hpp"

namespace Ble
{

/**
 * @brief   Outbound GATT-client operations on a single connection.
 * @note    Every call targets one connection handle. Implementations serialise
 *          access to the controller; callers must respect the one-procedure-at-
 *          a-time rule (issue the next procedure only after the current one has
 *          completed).
 */
class IGattClientOps {
public:

    /**
     * @brief   Discover a primary service by its 128-bit UUID.
     * @param   connHandle: connection to discover on.
     * @param   uuid: 128-bit service UUID to look for.
     * @retval  'true' if the procedure was accepted by the controller.
     */
    virtual bool discoverPrimaryServiceByUuid(uint16_t connHandle,
            const Ble::UUID& uuid) = 0;

    /**
     * @brief   Discover all characteristics within a service handle range.
     * @param   connHandle: connection to discover on.
     * @param   startHandle: service start (declaration) handle.
     * @param   endHandle: service group-end handle.
     * @retval  'true' if the procedure was accepted by the controller.
     */
    virtual bool discoverAllChars(uint16_t connHandle, uint16_t startHandle,
            uint16_t endHandle) = 0;

    /**
     * @brief   Discover all characteristic descriptors in a handle range.
     * @param   connHandle: connection to discover on.
     * @param   startHandle: first handle to search from.
     * @param   endHandle: last handle to search.
     * @retval  'true' if the procedure was accepted by the controller.
     */
    virtual bool discoverAllCharDescriptors(uint16_t connHandle,
            uint16_t startHandle, uint16_t endHandle) = 0;

    /**
     * @brief   Write a characteristic value (e.g. a control command).
     * @param   connHandle: connection to write on.
     * @param   valueHandle: characteristic value handle.
     * @param   data: bytes to write.
     * @param   len: number of bytes.
     * @retval  'true' if the write was accepted by the controller.
     */
    virtual bool writeCharValue(uint16_t connHandle, uint16_t valueHandle,
            const uint8_t* data, uint16_t len) = 0;

    /**
     * @brief   Write a characteristic descriptor (e.g. a CCCD to subscribe).
     * @param   connHandle: connection to write on.
     * @param   descHandle: descriptor handle.
     * @param   data: bytes to write.
     * @param   len: number of bytes.
     * @retval  'true' if the write was accepted by the controller.
     */
    virtual bool writeCharDescriptor(uint16_t connHandle, uint16_t descHandle,
            const uint8_t* data, uint16_t len) = 0;

protected:
    virtual ~IGattClientOps() = default;
};

} // namespace Ble

#endif // BLE_IGATT_CLIENT_OPS_HPP
