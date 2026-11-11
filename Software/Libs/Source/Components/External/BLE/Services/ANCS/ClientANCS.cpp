/**
 ******************************************************************************
 * @file    ClientANCS.cpp
 * @brief   The Apple Notification Center Service (ANCS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.ClientANCS"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "ClientANCS.hpp"

namespace Ble
{

///< CCCD value that enables notifications.
static constexpr uint16_t kCccdNotify = 0x0001;
///< Delay between the two CCCD writes (one client procedure at a time).
static constexpr uint32_t kSubscribeStepMs = 100;
///< Delay before retrying a CCCD write the controller rejected.
static constexpr uint32_t kRetryMs = 400;
///< Bounded retries for a subscribe write (controller momentarily busy).
static constexpr uint8_t kMaxSubRetry = 5;

void ClientANCS::init()
{
    mSrvUUID.fromString("7905f431-b5ce-4e99-a40f-4b1e122d00d0");
    mChars[CHR_NOTIFICATION_SOURCE].uuid.fromString("9fbf120d-6301-42d9-8c58-25e699a21dbd");
    mChars[CHR_CONTROL_POINT].uuid.fromString("69d1d8f3-45e1-49a8-9821-9bbdfdaad9d9");
    mChars[CHR_DATA_SOURCE].uuid.fromString("22eac6e9-24d6-4bb5-be44-b36ace7c7bfb");

    mTimer.init(LOG_MODULE_PRX, this, &ClientANCS::timCallback, false, this);

    blePeripheralClients().registerClient(this);
}

void ClientANCS::attachANCSCallback(Callback* pCallback)
{
    mpCallback = pCallback;
}

bool ClientANCS::writeControlPoint(const uint8_t* pData, uint16_t len)
{
    if (mChars[CHR_CONTROL_POINT].valueHandle == 0 ||
            mChars[CHR_DATA_SOURCE].valueHandle == 0) {
        return false;
    }

    // A link permits one GATT-client procedure at a time: defer until discovery
    // of all clients has finished, otherwise this write collides (0x46). The
    // consumer waits for onReady() before requesting, so this is a rare safety
    // net - keep it quiet.
    if (!blePeripheralClients().isReady()) {
        LOG_DEBUG("Control Point write deferred: discovery in progress\n");
        return false;
    }

    return blePeripheralClients().ops().writeCharValue(blePeripheralClients().connHandle(),
            mChars[CHR_CONTROL_POINT].valueHandle, pData, len);
}

void ClientANCS::copyHandles(const ResolvedService& res)
{
    for (CharInfo& info : mChars) {
        info.reset();
    }

    for (uint8_t i = 0; i < res.charCount; i++) {
        const ResolvedChar& rc = res.chars[i];
        for (CharInfo& info : mChars) {
            if (rc.uuid == info.uuid) {
                info.valueHandle = rc.valueHandle;
                info.cccdHandle = rc.cccdHandle;
                info.cccdFromDescriptor = rc.cccdFromDescriptor;
                break;
            }
        }
    }
}

void ClientANCS::onServicesResolved(const ResolvedService& res)
{
    copyHandles(res);
    mRetry = 0;

    LOG_DEBUG("Resolved: NotifSrc=0x%04X CtrlPt=0x%04X DataSrc=0x%04X\n",
            mChars[CHR_NOTIFICATION_SOURCE].valueHandle,
            mChars[CHR_CONTROL_POINT].valueHandle,
            mChars[CHR_DATA_SOURCE].valueHandle);

    if (mChars[CHR_NOTIFICATION_SOURCE].valueHandle == 0) {
        LOG_WARNING("Mandatory characteristic 'Notification Source' not found\n");
        blePeripheralClients().clientReady();
        return;
    }

    if (mChars[CHR_CONTROL_POINT].valueHandle == 0 ||
            mChars[CHR_DATA_SOURCE].valueHandle == 0) {
        LOG_WARNING("Optional characteristics 'Control Point' and/or 'Data Source' not found\n");
        mSetup = Setup::SUBSCRIBE_NOTIFICATION;
    } else {
        mSetup = Setup::SUBSCRIBE_DATA;
    }

    // A CCCD that descriptor discovery did not confirm is the value+1 fallback.
    // For ANCS that position is usually correct, but flag it so a missing-
    // notification symptom is diagnosable rather than silent.
    if (!mChars[CHR_NOTIFICATION_SOURCE].cccdFromDescriptor) {
        LOG_WARNING("Notification Source CCCD unconfirmed, using fallback 0x%04X\n",
                mChars[CHR_NOTIFICATION_SOURCE].cccdHandle);
    }
    if (mSetup == Setup::SUBSCRIBE_DATA &&
            !mChars[CHR_DATA_SOURCE].cccdFromDescriptor) {
        LOG_WARNING("Data Source CCCD unconfirmed, using fallback 0x%04X\n",
                mChars[CHR_DATA_SOURCE].cccdHandle);
    }

    mTimer.start(kSubscribeStepMs);
}

void ClientANCS::timCallback(void* arg)
{
    (void) arg;
    // The timer fires on the RTOS timer-daemon task; the setup step mutates
    // client state and issues blocking GATT writes, so run it on the BLE event
    // thread. If the work queue is momentarily full, re-arm and retry.
    if (!blePeripheralClients().postToBleThread(&ClientANCS::workTrampoline,
            this)) {
        mTimer.start(kSubscribeStepMs);
    }
}

void ClientANCS::workTrampoline(void* ctx)
{
    static_cast<ClientANCS*>(ctx)->runSetupStep();
}

void ClientANCS::runSetupStep()
{
    switch (mSetup) {
        case Setup::SUBSCRIBE_DATA:
            subscribeData();
            break;
        case Setup::SUBSCRIBE_NOTIFICATION:
            subscribeNotification();
            break;
        default:
            break;
    }
}

void ClientANCS::subscribeData()
{
    uint16_t attrVal = kCccdNotify;

    if (blePeripheralClients().ops().writeCharDescriptor(blePeripheralClients().connHandle(),
            mChars[CHR_DATA_SOURCE].cccdHandle, (uint8_t*) &attrVal,
            sizeof(attrVal))) {
        LOG_DEBUG("Subscribed Data Source (cccd=0x%04X)\n",
                mChars[CHR_DATA_SOURCE].cccdHandle);
        mRetry = 0;
        mSetup = Setup::SUBSCRIBE_NOTIFICATION;
        mTimer.start(kSubscribeStepMs);
        return;
    }

    if (++mRetry <= kMaxSubRetry) {
        LOG_DEBUG("Data Source subscribe retry %u/%u\n", mRetry, kMaxSubRetry);
        mTimer.start(kRetryMs);
        return;
    }

    // Cannot subscribe; release the coordinator so other clients proceed.
    LOG_WARNING("Data Source subscribe failed\n");
    mRetry = 0;
    mSetup = Setup::IDLE;
    blePeripheralClients().clientReady();
}

void ClientANCS::subscribeNotification()
{
    uint16_t attrVal = kCccdNotify;

    if (blePeripheralClients().ops().writeCharDescriptor(blePeripheralClients().connHandle(),
            mChars[CHR_NOTIFICATION_SOURCE].cccdHandle, (uint8_t*) &attrVal,
            sizeof(attrVal))) {
        LOG_DEBUG("Subscribed Notification Source (cccd=0x%04X)\n",
                mChars[CHR_NOTIFICATION_SOURCE].cccdHandle);
        mRetry = 0;
        mSetup = Setup::READY;
        LOG_INFO("Ready\n");
        blePeripheralClients().clientReady();
        return;
    }

    if (++mRetry <= kMaxSubRetry) {
        LOG_DEBUG("Notification Source subscribe retry %u/%u\n", mRetry,
                kMaxSubRetry);
        mTimer.start(kRetryMs);
        return;
    }

    LOG_WARNING("Notification Source subscribe failed\n");
    mRetry = 0;
    mSetup = Setup::IDLE;
    blePeripheralClients().clientReady();
}

void ClientANCS::onServiceUnavailable()
{
    LOG_INFO("Not available\n");
    mSetup = Setup::IDLE;
}

void ClientANCS::onNotification(uint16_t attrHandle, const uint8_t* data,
        uint8_t len)
{
    if (mpCallback == nullptr) {
        LOG_WARNING("Notification dropped: no callback attached\n");
        return;
    }

    if (attrHandle == mChars[CHR_NOTIFICATION_SOURCE].valueHandle) {
        LOG_DEBUG("Notification Source, len=%u\n", len);
        mpCallback->onNotifSource(data, len);
    } else if (attrHandle == mChars[CHR_DATA_SOURCE].valueHandle) {
        LOG_DEBUG("Data Source, len=%u\n", len);
        mpCallback->onDataSource(data, len);
    }
}

void ClientANCS::onLinkDown()
{
    // Stop the setup timer so no step fires against the torn-down link. A step
    // already queued on the BLE thread runs in Setup::IDLE and is a no-op.
    mTimer.stop();
    mSetup = Setup::IDLE;
    mRetry = 0;
    for (CharInfo& info : mChars) {
        info.reset();
    }
}

void ClientANCS::onLinkReady()
{
    // Link discovery is complete: Control Point requests no longer collide with
    // it. Let the consumer release any requests it was holding.
    if (mpCallback != nullptr) {
        mpCallback->onReady();
    }
}

} // namespace Ble
