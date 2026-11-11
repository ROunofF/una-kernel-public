/**
 ******************************************************************************
 * @file    CustomCommandService.hpp
 * @brief   Custom Command Service.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_SERVICE_CCS_HPP
#define __COMPONENT_BLE_SERVICE_CCS_HPP

#include "IBle.hpp"

namespace Ble
{

/**
 * @brief   BLE BCustom Command Service (CCS).
 */
class CustomCommandService : virtual public Interface::IBle::StatusCallback,
        virtual public Interface::IBle::CharacteristicCallback {
public:

    class Callback {
    public:
        virtual void onRebootReq(){}
        virtual bool onFwUpdateReq(const char *path){ return false; }
        virtual void onFindPhoneConfirmation(){}
        virtual void onReadDailyHealth(uint16_t year, uint8_t month, uint8_t day,
                uint32_t &steps, uint32_t &floors, uint32_t &actMin, uint32_t &RHR, uint32_t &AHR){}
        virtual void onReadEpoPeriod(uint32_t &sgps, uint32_t &egps,
                uint32_t &sglo, uint32_t &eglo, uint32_t &sgal, uint32_t &egal,
                uint32_t &sbei, uint32_t &ebei, uint32_t &sqzs, uint32_t &eqzs) {}
        virtual void onWriteReferenceLocation(float lat, float lon, float alt) {}
        virtual void onReadDailyHealthHrMatrix(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                uint8_t (&out)[60]){}

        /**
         * @brief   The phone subscribed to, or unsubscribed from, the event
         *          characteristic. State that is otherwise only pushed when it
         *          changes has to be sent once on subscribe, or a phone that
         *          connects into a standing condition never learns about it.
         * @param   enabled: 'true' when notifications were just enabled.
         */
        virtual void onEventNotificationsChanged(bool enabled) {}

    protected:
        virtual ~Callback() = default;
    };

    /**
     * @brief   Constructor.
     * @param   ble: BLE instance.
     * @param   pCallback: Operation callback.
     */
    CustomCommandService(Interface::IBle &ble, Callback *pCallback);

    /**
     * @brief   Destructor.
     */
    virtual ~CustomCommandService();



    bool sendEventActivityEnded(uint64_t appId);
    bool sendEventFindPhoneAlert(bool active);

    /**
     * @brief   Reports the system error the watch is currently in.
     * @param   code: SystemError value, 0 for no error.
     * @retval  'true' if the event was sent.
     */
    bool sendEventSystemError(uint8_t code);

    /**
     * @brief   Consumer of media Now Playing updates (CAMS, see Services/CAMS).
     */
    class IMediaCommandSink {
    public:
        /**
         * @brief   A Now Playing update arrived on the media command.
         * @param   pData: the full command packet.
         * @param   len: packet length in bytes.
         * @retval  'true' if the packet was applied, 'false' if it was malformed.
         */
        virtual bool onMediaNowPlaying(const uint8_t *pData, uint16_t len) = 0;

    protected:
        virtual ~IMediaCommandSink() = default;
    };

    /**
     * @brief   Registers the consumer of media Now Playing updates.
     */
    void setMediaCommandSink(IMediaCommandSink *pSink);

    /**
     * @brief   Sends a media remote command to the phone.
     * @param   command: a Ble::CAMS::MediaCommandID value.
     * @retval  'true' if the event was sent.
     */
    bool sendEventMediaRemoteCommand(uint8_t command);


private:

    Interface::IBle &mBle;
    Callback *mObserver = nullptr;
    IMediaCommandSink *mMediaSink = nullptr;

    ServiceHandle mSrv {};
    CharacteristicHandle mChrCmd {};

    static constexpr const uint32_t skChrEvtLength = 20;
    CharacteristicHandle mChrEvt {};

    bool mCmdNotificationEnabled = false;
    bool mEvtNotificationEnabled = false;

    struct CommandId {
        enum : uint8_t {
            INVALID = 0x00,
            RESET = 0x01,
            START_FW_UPDATE = 0x2,

            GET_DAILY_HEALTH = 0x10,
            FIND_PHONE_CONFIRM = 0x11,

            GET_EPO_STATUS = 0x12,
            SET_REF_LOCATION = 0x13,

            GET_DAILY_HEALTH_HR = 0x14,

        };
    };

    struct ResponseStatus {
        enum {
            INVALID = 0x00,
            OK = 0x01,
            ERROR = 0x02,
        };
    };

    struct EventId {
        enum : uint8_t {
            INVALID = 0x00,
            ACTIVITY_SESSION_ENDED = 0x01,
            FIND_PHONE_ALERT = 0x02,
            MEDIA_REMOTE_COMMAND = 0x03,
            SYSTEM_ERROR = 0x04,

        };
    };

#pragma pack(push, 1)
    // Common error response
    struct ErrorResponse {
        uint8_t command;
        uint8_t status;

        ErrorResponse() {}
        ErrorResponse(uint8_t cmd, uint8_t status) : command(cmd), status(status) {}
    };

    // 0x01 - Reset request
    struct ResetReq {
        uint8_t command = CommandId::RESET;
        uint8_t reserved = 0;   // padding
    };
    struct ResetRsp {
        uint8_t command = CommandId::RESET;
        uint8_t status;
    };

    // 0x02 - Start Firmware Update request
    struct StartFirmwareUpdateReq {
        uint8_t command = CommandId::START_FW_UPDATE;
        uint8_t reserved = 0;   // padding
        uint16_t pathLength;    // length of the path
        // actual path
    };
    struct StartFirmwareUpdateRsp {
        uint8_t command = CommandId::START_FW_UPDATE;
        uint8_t status;
    };

    // 0x10 - Daily Health
    struct DailyHealthReq {
        uint8_t command = CommandId::GET_DAILY_HEALTH;
        uint8_t reserved = 0;   // padding
        uint16_t year;
        uint8_t month;
        uint8_t day;
    };
    struct DailyHealthRsp {
        uint8_t command = CommandId::GET_DAILY_HEALTH;
        uint8_t status;
        uint32_t steps;
        uint32_t floors;
        uint32_t activityMinutes;
        uint32_t restingHeartRate;
        uint32_t averageHeartRate;
    };

    // 0x12 - Get EPO files period
    struct EpoStatusReq {
        uint8_t command = CommandId::GET_EPO_STATUS;
        uint8_t reserved = 0;   // padding
    };
    struct EpoStatusRsp {
        uint8_t command = CommandId::GET_EPO_STATUS;
        uint8_t status;
        uint32_t sgps;
        uint32_t egps;
        uint32_t sglo;
        uint32_t eglo;
        uint32_t sgal;
        uint32_t egal;
        uint32_t sbei;
        uint32_t ebei;
        uint32_t sqzs;
        uint32_t eqzs;
    };

    // 0x13 - Set reference location
    struct ReferenceLocationReq {
        uint8_t command = CommandId::SET_REF_LOCATION;
        uint8_t reserved = 0;   // padding
        float lat;
        float lon;
        float alt;
    };
    struct ReferenceLocationRsp {
        uint8_t command = CommandId::SET_REF_LOCATION;
        uint8_t status;
    };

    // 0x11 - Find Phone alert confirmation
    struct FindPhoneAlertConfirmationReq {
        uint8_t command = CommandId::FIND_PHONE_CONFIRM;
        uint8_t reserved = 0;   // padding
    };
    struct FindPhoneAlertConfirmationRsp {
        uint8_t command = CommandId::FIND_PHONE_CONFIRM;
        uint8_t status;
    };

    // Events
    // 0x01 Activity Session Ended
    struct EvtActivitySessionEnded {
        uint8_t evt = EventId::ACTIVITY_SESSION_ENDED;
        uint8_t reserved = 0;   // padding
        uint64_t appId;
    };

    // 0x02 - Find Phone alert
    struct EvtFindPhoneAlert {
        uint8_t evt = EventId::FIND_PHONE_ALERT;
        uint8_t state = 0;
    };

    // 0x04 - System error
    struct EvtSystemError {
        uint8_t evt = EventId::SYSTEM_ERROR;
        uint8_t reserved = 0;   // padding
        uint8_t code = 0;       // SystemError value, 0 = no error
    };

    // 0x14 - Daily Health HR matrix
    struct DailyHealthHrReq {
        uint8_t command = CommandId::GET_DAILY_HEALTH_HR;
        uint8_t reserved = 0;   // padding
        uint16_t year;
        uint8_t month;
        uint8_t day;
        uint8_t hour;
    };
    struct DailyHealthHrRsp {
        uint8_t command = CommandId::GET_DAILY_HEALTH_HR;
        uint8_t status;
        uint8_t hourMatrix[60];
    };

#pragma pack(pop)

    /**
     * @copydoc Interface::IBle::StatusCallback::onDisconnect()
     */
    virtual void onDisconnect(uint8_t reason) override;

    /**
     * @brief   Called when a characteristic's notification or indication
     *          is enabled/disabled.
     * @param   chrHandle: Handle of the characteristic.
     * @param   notify: 'true' if notifications are enabled, 'false' disabled.
     * @param   indicate:'true' if indications are enabled, 'false' disabled.
     */
    virtual void onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
            bool notify, bool indicate) override;

    /**
     * @brief   Called when data is written to a characteristic by the
     *          remote device.
     * @param   chrHandle: Handle of the characteristic being written to.
     * @param   pData: Pointer to the data that was written.
     * @param   len: Length of the data in bytes.
     */
    virtual void onWrite(Ble::CharacteristicHandle chrHandle,
            const uint8_t *pData, uint16_t len) override;


    void resetHandler(const uint8_t *pData, uint16_t len);
    void firmwareUpdateHandler(const uint8_t *pData, uint16_t len);
    void dailyHealthHandler(const uint8_t *pData, uint16_t len);
    void findPhoneConfirmationHandler(const uint8_t *pData, uint16_t len);
    void epoStatusHandler(const uint8_t *pData, uint16_t len);
    void refLocationHandler(const uint8_t *pData, uint16_t len);
    void dailyHealthHrHandler(const uint8_t *pData, uint16_t len);
    void mediaNowPlayingHandler(const uint8_t *pData, uint16_t len);

    void sendResponce(const uint8_t *pData, uint16_t len);
    void sendStatus(uint8_t cmd, uint8_t status);

};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_SERVICE_CCS_HPP */
