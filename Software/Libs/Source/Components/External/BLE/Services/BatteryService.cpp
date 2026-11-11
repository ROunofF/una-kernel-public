/**
 ******************************************************************************
 * @file    BatteryService.cpp
 * @brief   BLE Battery Service (BAS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Services/BatteryService.hpp"

#define LOG_MODULE_PRX      "BLE.SRV.Battery"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace Ble
{

BatteryService::BatteryService(Interface::IBle &ble, Callback *pCallback) :
        mBle(ble), mObserver(pCallback)
{
    // Create the BAS service
    mSrv = mBle.addService(UUID(0x180F));

    // Add Current Time characteristic
    mChrBatteryLevel = mBle.addCharacteristic(mSrv, UUID(0x2A19),
            sizeof(mBatteryLevel), (CharProp::READ | CharProp::NOTIFY));
    mBle.attachCharacteristicCallback(mChrBatteryLevel, this);

    mBle.attachStatusCallback(this);
}

BatteryService::~BatteryService()
{
    mBle.detachStatusCallback(this);

    mBle.attachCharacteristicCallback(mChrBatteryLevel, nullptr);
    mBle.delCharacteristic(mChrBatteryLevel);

    mBle.delService(mSrv);
}

void BatteryService::onDisconnect(uint8_t reason)
{
    mNotificationEnabled = false;
}

void BatteryService::onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
        bool notify, bool indicate)
{
    if (chrHandle == mChrBatteryLevel) {
        mNotificationEnabled = notify;
        LOG_DEBUG("Notification %s\n",mNotificationEnabled ? "enabled" : "disabled");
        if (mNotificationEnabled ) {
            float newLevel = 0.0f;
            if (mObserver) {
                mObserver->onReadBatteryLevel(newLevel);
            }
            uint8_t u8Level = static_cast<uint8_t>(newLevel);
            if (mBatteryLevel != u8Level) {
                mBatteryLevel = u8Level;
                LOG_INFO("Battery level %u%%\n", mBatteryLevel);
                mBle.setCharacteristicValue(mChrBatteryLevel,
                        (const uint8_t*)&mBatteryLevel, sizeof(mBatteryLevel));
            }
        }
    }
}

void BatteryService::onRead(Ble::CharacteristicHandle chrHandle)
{
    if (chrHandle == mChrBatteryLevel) {

        float newLevel = 0.0f;
        if (mObserver) {
            mObserver->onReadBatteryLevel(newLevel);
        }
        mBatteryLevel = static_cast<uint8_t>(newLevel);
        LOG_INFO("Battery level %u%%\n", mBatteryLevel);
        mBle.setCharacteristicValue(mChrBatteryLevel,
                (const uint8_t*)&mBatteryLevel, sizeof(mBatteryLevel));
    }
}

void BatteryService::onBatteryLevelChanged(float level)
{
    if (mNotificationEnabled) {
        uint8_t u8Level = static_cast<uint8_t>(level);
        if (mBatteryLevel != u8Level) {
            mBatteryLevel = u8Level;
            LOG_INFO("Battery level %u%%\n", mBatteryLevel);
            mBle.setCharacteristicValue(mChrBatteryLevel,
                    (const uint8_t*)&mBatteryLevel, sizeof(mBatteryLevel));
        }
    }
}

}  /* namespace Ble */
