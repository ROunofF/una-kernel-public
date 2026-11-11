/**
 ******************************************************************************
 * @file    CustomCommandService.cpp
 * @brief   Custom Command Service.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Services/CustomCommandService.hpp"

#include "Services/CAMS/ParserCAMS.hpp"

#define LOG_MODULE_PRX      "BLE.SRV.CustomCommand"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"


namespace Ble
{

CustomCommandService::CustomCommandService(Interface::IBle &ble,
        Callback *pCallback) :
        mBle(ble), mObserver(pCallback)
{
    // Create the Custom Command service
    mSrv = mBle.addService(UUID("554E4100-A2CF-4DF8-0000-7E1E48595106"));

    // Add Command characteristic characteristic
    mChrCmd = mBle.addCharacteristic(mSrv,
            UUID("554E4100-A2CF-4DF8-0001-7E1E48595106"), mBle.getMaxCharacteristicLen(),
            (CharProp::WRITE_WO_RESP | CharProp::NOTIFY));
    mBle.attachCharacteristicCallback(mChrCmd, this);

    // Add Event characteristic characteristic
    mChrEvt = mBle.addCharacteristic(mSrv,
            UUID("554E4100-A2CF-4DF8-0002-7E1E48595106"), skChrEvtLength,
            (CharProp::NOTIFY));
    mBle.attachCharacteristicCallback(mChrEvt, this);

    mBle.attachStatusCallback(this);
}

CustomCommandService::~CustomCommandService()
{
    mBle.detachStatusCallback(this);

    mBle.attachCharacteristicCallback(mChrEvt, nullptr);
    mBle.delCharacteristic(mChrEvt);

    mBle.attachCharacteristicCallback(mChrCmd, nullptr);
    mBle.delCharacteristic(mChrCmd);

    mBle.delService(mSrv);
}

bool CustomCommandService::sendEventActivityEnded(uint64_t appId)
{
    if (!mEvtNotificationEnabled) {
        return false;
    }

    EvtActivitySessionEnded evt {};
    evt.appId = appId;

    LOG_INFO("Activity Ended event. "
            "App ID: '%02X%02X%02X%02X%02X%02X%02X%02X'\n",
            (uint8_t)((appId >> 56) & 0xFF),
            (uint8_t)((appId >> 48) & 0xFF),
            (uint8_t)((appId >> 40) & 0xFF),
            (uint8_t)((appId >> 32) & 0xFF),
            (uint8_t)((appId >> 24) & 0xFF),
            (uint8_t)((appId >> 16) & 0xFF),
            (uint8_t)((appId >> 8)  & 0xFF),
            (uint8_t)((appId >> 0)  & 0xFF));

    return mBle.setCharacteristicValue(mChrEvt,
            reinterpret_cast<const uint8_t*>(&evt), sizeof(evt));
}

bool CustomCommandService::sendEventFindPhoneAlert(bool active)
{
    if (!mEvtNotificationEnabled) {
        return false;
    }

    EvtFindPhoneAlert alert {};
    alert.state = active ? 1 : 0;

    LOG_INFO("Find Phone Alert event: %s", active ? "active" : "stop");

    return mBle.setCharacteristicValue(mChrEvt,
            reinterpret_cast<const uint8_t*>(&alert), sizeof(alert));
}

bool CustomCommandService::sendEventSystemError(uint8_t code)
{
    if (!mEvtNotificationEnabled) {
        return false;
    }

    EvtSystemError evt {};
    evt.code = code;

    LOG_INFO("System Error event: %u\n", code);

    return mBle.setCharacteristicValue(mChrEvt,
            reinterpret_cast<const uint8_t*>(&evt), sizeof(evt));
}

void CustomCommandService::setMediaCommandSink(IMediaCommandSink *pSink)
{
    mMediaSink = pSink;
}

bool CustomCommandService::sendEventMediaRemoteCommand(uint8_t command)
{
    if (!mEvtNotificationEnabled) {
        return false;
    }

    uint8_t payload[CAMS::kRemoteCommandEventLen];
    uint16_t len = CAMS::BuildRemoteCommandEvent(command, payload, sizeof(payload));
    if (len == 0) {
        return false;
    }

    return mBle.setCharacteristicValue(mChrEvt, payload, len);
}

void CustomCommandService::onDisconnect(uint8_t reason)
{
    mCmdNotificationEnabled = false;
    mEvtNotificationEnabled = false;
}

void CustomCommandService::onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
        bool notify, bool indicate)
{
    if (chrHandle == mChrCmd) {
        mCmdNotificationEnabled = notify;
        LOG_DEBUG("Command Notification %s\n",mCmdNotificationEnabled ? "enabled" : "disabled");
    }

    if (chrHandle == mChrEvt) {
        mEvtNotificationEnabled = notify;
        LOG_DEBUG("Event Notification %s\n",mEvtNotificationEnabled ? "enabled" : "disabled");

        // Flag first, then tell the observer: it answers by sending an event,
        // which the send path drops unless notifications are already marked on.
        if (mObserver) {
            mObserver->onEventNotificationsChanged(mEvtNotificationEnabled);
        }
    }
}

void CustomCommandService::onWrite(Ble::CharacteristicHandle chrHandle,
            const uint8_t *pData, uint16_t len)
{
    if (chrHandle == mChrCmd) {

        if (!mCmdNotificationEnabled) {
            return;
        }

        LOG_DEBUG("Request %d bytes:\n", len);
        LOG_DEBUG_DUMP(pData, len);

        uint8_t command = pData[0];
        switch (command) {

            case CommandId::RESET:
                resetHandler(pData, len);
                break;

            case CommandId::START_FW_UPDATE:
                firmwareUpdateHandler(pData, len);
                break;

            case CommandId::GET_DAILY_HEALTH:
                dailyHealthHandler(pData, len);
                break;

            case CommandId::FIND_PHONE_CONFIRM:
                findPhoneConfirmationHandler(pData, len);
                break;

            case CommandId::GET_EPO_STATUS:
                epoStatusHandler(pData, len);
                break;

            case CommandId::SET_REF_LOCATION:
                refLocationHandler(pData, len);
                break;

            case CommandId::GET_DAILY_HEALTH_HR:
                dailyHealthHrHandler(pData, len);
                break;

            case CAMS::kCmdNowPlaying:
                mediaNowPlayingHandler(pData, len);
                break;

            default:
                break;
        }
    }
}

void CustomCommandService::resetHandler(const uint8_t *pData, uint16_t len)
{
    const ResetReq &cmd = reinterpret_cast<const ResetReq&>(*pData);

    // Validate command length
    uint16_t expectedLength = sizeof(ResetReq);
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendStatus(cmd.command, ResponseStatus::ERROR);
        return;
    }

    ResetRsp rsp {};
    rsp.status = ResponseStatus::OK;
    sendResponce(reinterpret_cast<const uint8_t*>(&rsp), sizeof(rsp));
    LOG_INFO("Reset request\n");
    if (mObserver) {
        mObserver->onRebootReq();
    }
}

void CustomCommandService::firmwareUpdateHandler(const uint8_t *pData, uint16_t len)
{
    // Test command: /Update/test.ota
    // 02 00 10 00 2F 55 70 64 61 74 65 2F 74 65 73 74 2E 6F 74 61


    // Test command: /Update/firmware.ota
    // 02 00 14 00 2F 55 70 64 61 74 65 2F 66 69 72 6D 77 61 72 65 2E 6F 74 61
    const StartFirmwareUpdateReq &cmd = reinterpret_cast<const StartFirmwareUpdateReq&>(*pData);

    // Validate command length
    uint16_t expectedLength = sizeof(StartFirmwareUpdateReq) + cmd.pathLength;
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendStatus(cmd.command, ResponseStatus::ERROR);
        return;
    }

    // NOTE! It is not NULL terminated path
    const char *relPath = reinterpret_cast<const char*>(&pData[sizeof(StartFirmwareUpdateReq)]);

    bool status = false;
    if (mObserver) {
        char path[cmd.pathLength + 1] {};
        snprintf(path, sizeof(path), "%.*s", cmd.pathLength, relPath);
        status = mObserver->onFwUpdateReq(path);
    }
    LOG_INFO("Firmware Update %s\n", status ? "allowed" : "failed");

    if (!status) {
        sendStatus(cmd.command, ResponseStatus::ERROR);
        return;
    }

    StartFirmwareUpdateRsp rsp {};
    rsp.status = ResponseStatus::OK;
    sendResponce(reinterpret_cast<const uint8_t*>(&rsp), sizeof(rsp));
}

void CustomCommandService::dailyHealthHandler(const uint8_t *pData, uint16_t len)
{
    const DailyHealthReq &cmd = reinterpret_cast<const DailyHealthReq&>(*pData);

    // Validate command length
    uint16_t expectedLength = sizeof(DailyHealthReq);
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendStatus(cmd.command, ResponseStatus::ERROR);
        return;
    }

    DailyHealthRsp rsp {};
    rsp.status = ResponseStatus::OK;

    if (mObserver) {
        mObserver->onReadDailyHealth(cmd.year, cmd.month, cmd.day,
                rsp.steps, rsp.floors, rsp.activityMinutes, rsp.restingHeartRate, rsp.averageHeartRate);
    }
    LOG_INFO("Request DH %04u-%02u-%02u: steps %u, floors %u, act %u, RHR %u, AHR %u\n",
            cmd.year, cmd.month, cmd.day,
            rsp.steps, rsp.floors, rsp.activityMinutes, rsp.restingHeartRate, rsp.averageHeartRate);
    sendResponce(reinterpret_cast<const uint8_t*>(&rsp), sizeof(rsp));
}

void CustomCommandService::findPhoneConfirmationHandler(const uint8_t *pData, uint16_t len)
{
    if (mObserver) {
        mObserver->onFindPhoneConfirmation();
    }

    LOG_INFO("Find Phone Confirmed\n");

    FindPhoneAlertConfirmationRsp rsp {};
    rsp.status = ResponseStatus::OK;
    sendResponce(reinterpret_cast<const uint8_t*>(&rsp), sizeof(rsp));
}

void CustomCommandService::epoStatusHandler(const uint8_t *pData, uint16_t len)
{
    const EpoStatusReq &cmd = reinterpret_cast<const EpoStatusReq&>(*pData);

    // Validate command length
    uint16_t expectedLength = sizeof(EpoStatusReq);
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendStatus(cmd.command, ResponseStatus::ERROR);
        return;
    }

    EpoStatusRsp rsp {};

    if (mObserver) {
        mObserver->onReadEpoPeriod(rsp.sgps, rsp.egps, rsp.sglo, rsp.eglo, rsp.sgal, rsp.egal, rsp.sbei, rsp.ebei, rsp.sqzs, rsp.eqzs);
    }
    LOG_INFO("Request EPO. GPS: %u-%u, GLO: %u-%u, GAL: %u-%u, BD: %u-%u, QZZS: %u-%u\n",
            rsp.sgps, rsp.egps, rsp.sglo, rsp.eglo, rsp.sgal, rsp.egal, rsp.sbei, rsp.ebei, rsp.sqzs, rsp.eqzs);

    rsp.status = ResponseStatus::OK;
    sendResponce(reinterpret_cast<const uint8_t*>(&rsp), sizeof(rsp));
}

void CustomCommandService::refLocationHandler(const uint8_t *pData, uint16_t len)
{
    const ReferenceLocationReq &cmd =
            reinterpret_cast<const ReferenceLocationReq&>(*pData);

    // Validate command length
    uint16_t expectedLength = sizeof(ReferenceLocationReq);
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendStatus(cmd.command, ResponseStatus::ERROR);
        return;
    }

    if (mObserver) {
        LOG_INFO("Reference location received\n");
        mObserver->onWriteReferenceLocation(cmd.lat, cmd.lon, cmd.alt);
    }

    ReferenceLocationRsp rsp {};
    rsp.status = ResponseStatus::OK;

    sendResponce(reinterpret_cast<const uint8_t*>(&rsp), sizeof(rsp));
}

void CustomCommandService::dailyHealthHrHandler(const uint8_t *pData, uint16_t len)
{
    const DailyHealthHrReq &cmd = reinterpret_cast<const DailyHealthHrReq&>(*pData);

    // Validate command length
    uint16_t expectedLength = sizeof(DailyHealthHrReq);
    if (len != expectedLength) {
        LOG_WARNING("Wrong length. Expected %u, Got %u\n", expectedLength, len);
        sendStatus(cmd.command, ResponseStatus::ERROR);
        return;
    }

    DailyHealthHrRsp rsp {};
    rsp.status = ResponseStatus::OK;

    if (mObserver) {
        mObserver->onReadDailyHealthHrMatrix(cmd.year, cmd.month, cmd.day, cmd.hour, rsp.hourMatrix);
    }
    LOG_INFO("Request DH %04u-%02u-%02u, hour %u\n",
            cmd.year, cmd.month, cmd.day, cmd.hour);
    sendResponce(reinterpret_cast<const uint8_t*>(&rsp), sizeof(rsp));
}

void CustomCommandService::mediaNowPlayingHandler(const uint8_t *pData, uint16_t len)
{
    // Apply the Now Playing update via the registered media consumer and ack with
    // the standard [command][status] response.
    bool ok = (mMediaSink != nullptr) && mMediaSink->onMediaNowPlaying(pData, len);
    sendStatus(CAMS::kCmdNowPlaying, ok ? ResponseStatus::OK : ResponseStatus::ERROR);
}

void CustomCommandService::sendResponce(const uint8_t *pData, uint16_t len)
{
    LOG_DEBUG("Response %d bytes:\n", len);
    LOG_DEBUG_DUMP(pData, len);
    mBle.setCharacteristicValue(mChrCmd, pData, len);
}

void CustomCommandService::sendStatus(uint8_t cmd, uint8_t status)
{
    ErrorResponse err(cmd, status);

    LOG_DEBUG("Status %d\n", err);
    LOG_DEBUG_DUMP(&err, sizeof(err));

    sendResponce(reinterpret_cast<const uint8_t*>(&err), sizeof(err));
}

}  /* namespace Ble */
