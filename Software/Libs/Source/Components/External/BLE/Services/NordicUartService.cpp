/**
 ******************************************************************************
 * @file    NordicUartService.cpp
 * @brief   Nordic UART Service (NUS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Services/NordicUartService.hpp"

#define LOG_MODULE_PRX      "BLE.SRV.NordicUart"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

namespace Ble
{

NordicUartService::NordicUartService(Interface::IBle &ble, Callback *pCallback) :
        mBle(ble), mObserver(pCallback)
{
    // Create the NUS service
    mSrv = mBle.addService(UUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"));

    // Add TX characteristic
    mChrTx = mBle.addCharacteristic(mSrv, UUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"),
            mBle.getMaxCharacteristicLen(), CharProp::NOTIFY);
    mBle.attachCharacteristicCallback(mChrTx, this);

    // Add RX characteristic
    mChrRx = mBle.addCharacteristic(mSrv, UUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"),
            8, (CharProp::WRITE | CharProp::WRITE_WO_RESP));
    mBle.attachCharacteristicCallback(mChrRx, this);

    mBle.attachStatusCallback(this);
}

NordicUartService::~NordicUartService()
{
    mBle.detachStatusCallback(this);

    mBle.attachCharacteristicCallback(mChrRx, nullptr);
    mBle.delCharacteristic(mChrRx);

    mBle.attachCharacteristicCallback(mChrTx, nullptr);
    mBle.delCharacteristic(mChrTx);

    mBle.delService(mSrv);
}

void NordicUartService::onDisconnect(uint8_t reason)
{
    mNotificationEnabled = false;
}

void NordicUartService::onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
        bool notify, bool indicate)
{
    if (chrHandle == mChrTx) {
        mNotificationEnabled = notify;
        LOG_DEBUG("Notification %s\n",mNotificationEnabled ? "enabled" : "disabled");
    }
}

void NordicUartService::onWrite(Ble::CharacteristicHandle chrHandle,
        const uint8_t *pData, uint16_t len)
{
    if (chrHandle == mChrRx) {
        if (mObserver) {
            mObserver->onRx({reinterpret_cast<const char*>(pData), static_cast<size_t>(len)});
        }
    }
}

void NordicUartService::onTx(std::string_view str)
{
    if (mNotificationEnabled) {
        mBle.setCharacteristicValue(mChrTx, reinterpret_cast<const uint8_t*>(str.data()), str.size());
    }
}

}  /* namespace Ble */
