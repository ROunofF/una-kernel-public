/**
 ******************************************************************************
 * @file    DeviceInformationService.hpp
 * @brief   BLE Device Information Service (DIS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_SERVICE_DIS_HPP
#define __COMPONENT_BLE_SERVICE_DIS_HPP

#include "IBle.hpp"

namespace Ble
{

/**
 * @brief   BLE Device Information Service (DIS).
 */
class DeviceInformationService : virtual public Interface::IBle::StatusCallback {
public:

    /**
     * @brief   Constructor.
     * @param   ble: BLE instance.
     */
    DeviceInformationService(Interface::IBle &ble,
            const char *pManufacturerName,
            const char *pModelNumber,
            const char *pSerialNumber,
            const char *pFirmwareRevision,
            const char *pHardwareRevision);

    /**
     * @brief   Destructor.
     */
    virtual ~DeviceInformationService();


private:

    Interface::IBle &mBle;

    ServiceHandle mSrv { };
    CharacteristicHandle mChrManufacturerName { };
    CharacteristicHandle mChrModelNumber { };
    CharacteristicHandle mChrSerialNumber { };
    CharacteristicHandle mChrFirmwareRevision { };
    CharacteristicHandle mChrHardwareRevision { };

    const char *mpManufacturerName = nullptr;
    const char *mpModelNumber = nullptr;
    const char *mpSerialNumber = nullptr;
    const char *mpFirmwareRevision = nullptr;
    const char *mpHardwareRevision = nullptr;


    /**
     * @brief   Called when the stack is initialized and it is possible to
     *          perform operations with characteristics (update values, etc.).
     */
    virtual void onEnable() override;
};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_SERVICE_DIS_HPP */
