/**
 ******************************************************************************
 * @file    DeviceInformationService.cpp
 * @brief   BLE Device Information Service (DIS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Services/DeviceInformationService.hpp"

#define LOG_MODULE_PRX      "BLE.SRV.DeviceInformation"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cassert>

namespace Ble
{

DeviceInformationService::DeviceInformationService(Interface::IBle &ble,
        const char *pManufacturerName,
        const char *pModelNumber,
        const char *pSerialNumber,
        const char *pFirmwareRevision,
        const char *pHardwareRevision) :
                mBle(ble),
                mpManufacturerName(pManufacturerName),
                mpModelNumber(pModelNumber),
                mpSerialNumber(pSerialNumber),
                mpFirmwareRevision(pFirmwareRevision),
                mpHardwareRevision(pHardwareRevision)
{
    assert(mpManufacturerName != nullptr);
    assert(mpModelNumber != nullptr);
    assert(mpSerialNumber != nullptr);
    assert(mpFirmwareRevision != nullptr);
    assert(mpHardwareRevision != nullptr);

    // Create the DIS service
    mSrv = mBle.addService(UUID(0x180A));

    // Add Manufacturer Name String characteristic
    mChrManufacturerName = mBle.addCharacteristic(mSrv,
            UUID(0x2A29),
            strlen(mpManufacturerName),
            CharProp::READ);

    // Add Model Number String characteristic
    mChrModelNumber = mBle.addCharacteristic(mSrv,
            UUID(0x2A24),
            strlen(mpModelNumber),
            CharProp::READ);

    // Add Serial Number String characteristic
    mChrSerialNumber = mBle.addCharacteristic(mSrv,
            UUID(0x2A25),
            strlen(mpSerialNumber),
            CharProp::READ);

    // Add Firmware Revision String characteristic
    mChrFirmwareRevision = mBle.addCharacteristic(mSrv,
            UUID(0x2A26), strlen(mpFirmwareRevision),
            CharProp::READ);

    // Add Hardware Revision String characteristic
    mChrHardwareRevision = mBle.addCharacteristic(mSrv,
            UUID(0x2A27), strlen(mpHardwareRevision),
            CharProp::READ);

    // Register callback
    mBle.attachStatusCallback(this);
}

DeviceInformationService::~DeviceInformationService()
{
    mBle.detachStatusCallback(this);

    mBle.delCharacteristic(mChrHardwareRevision);
    mBle.delCharacteristic(mChrFirmwareRevision);
    mBle.delCharacteristic(mChrSerialNumber);
    mBle.delCharacteristic(mChrModelNumber);
    mBle.delCharacteristic(mChrManufacturerName);
    mBle.delService(mSrv);
}

void DeviceInformationService::onEnable()
{
    mBle.setCharacteristicValue(mChrManufacturerName,
            (const uint8_t*)mpManufacturerName,
            strlen(mpManufacturerName));

    mBle.setCharacteristicValue(mChrModelNumber,
            (const uint8_t*)mpModelNumber,
            strlen(mpModelNumber));

    mBle.setCharacteristicValue(mChrSerialNumber,
            (const uint8_t*)mpSerialNumber,
            strlen(mpSerialNumber));

    mBle.setCharacteristicValue(mChrFirmwareRevision,
            (const uint8_t*)mpFirmwareRevision,
            strlen(mpFirmwareRevision));

    mBle.setCharacteristicValue(mChrHardwareRevision,
            (const uint8_t*)mpHardwareRevision,
            strlen(mpHardwareRevision));
}

}  /* namespace Ble */
