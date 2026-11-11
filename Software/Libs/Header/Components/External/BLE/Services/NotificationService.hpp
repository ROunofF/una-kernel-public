/**
 ******************************************************************************
 * @file    NotificationService.hpp
 * @brief   Custom Notification Service (CNS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_SERVICE_CNS_HPP
#define __COMPONENT_BLE_SERVICE_CNS_HPP

#include <memory>
#include <array>

#include "IBle.hpp"
#include "ClientANCS.hpp"
#include "ParserANCS.hpp"
#include "ParserCANS.hpp"
#include "PhoneNotification.hpp"
#include "Observer.hpp"
#include "OS.hpp"

namespace Ble
{

/**
 * @brief   Custom Notification Service (CNS).
 */
class NotificationService : virtual public Interface::IBle::StatusCallback,
        virtual public Interface::IBle::CharacteristicCallback,
        public ClientANCS::Callback {
public:

    class Callback {
    public:
        virtual void onNotificationAdd(std::shared_ptr<PhoneNotification> &notification, bool isNew){}
        virtual void onNotificationRemove(uint32_t uid){}
        virtual void onNotificationRemoveAll(){}
    protected:
        virtual ~Callback() = default;
    };

    /**
     * @brief   Constructor.
     * @param   ble: BLE instance.
     */
    NotificationService(Interface::IBle &ble);

    /**
     * @brief   Destructor.
     */
    virtual ~NotificationService();

    void enablePhoneNotification(bool en);

    void attachCallback(Callback *pCallback);

    void detachCallback(Callback *pCallback);

    bool positiveAction(const std::shared_ptr<PhoneNotification> &notification);

    bool negativeAction(const std::shared_ptr<PhoneNotification> &notification);

private:

    Interface::IBle &mBle;

    ServiceHandle mSrvAndr { };
    CharacteristicHandle mChrAndrNotify  { };
    CharacteristicHandle mChrAndrComRsp { };

    bool mNotificationEnabled = false;

    bool mPhoneNotificationEnabled = false;

    uint32_t mChrAndrNotifyRxCRC = 0;

    /// Observer for callbacks.
    Observer::Holder<NotificationService::Callback, 5> mObserver;


    struct NotifInfo {
        PhoneNotification::Platform platform;
        uint8_t action;
        uint8_t category;
        uint32_t uid;
    };

    enum EventID {
      EVT_ID_NOTIF = 0,
      EVT_ID_CONNECT,
      EVT_ID_DISCONNECT,
    };

    struct QueueEvent {
        EventID evtID;
        uint32_t repeatCnt;
        union {
            NotifInfo notification;
        };
    };

    OS::Queue<QueueEvent, 50> mInNotifQueue;

    OS::Thread<NotificationService, 5*1024> mThread;

    struct Flag {
        static constexpr uint32_t AttrFetchSuccess   = (1 << 0);
        static constexpr uint32_t AttrFetchFail   = (1 << 1);
        static constexpr uint32_t AncsReady   = (1 << 2);
        static constexpr uint32_t LinkDown   = (1 << 3);
    };
    OS::EventFlags mFlags;
    static constexpr uint16_t skTimeout = 1*1000;  // TODO: Set small timeout after testing
    ///< Bound on how long a fetch waits for ANCS to become usable before it is
    ///< dropped (best-effort: a stale notification is skipped as old anyway).
    static constexpr uint32_t skAncsReadyWaitMs = 10*1000;

    ANCS::AttributeParser mParserIOS;
    CANS::AttributeParser mParserANDROID;

    static constexpr uint16_t skBufferSize = 2048;
    uint8_t mBuffer[skBufferSize] {};
    uint16_t mBufferLen = 0;

    /**
   * @copydoc Interface::IBle::StatusCallback::onConnect()
     */
    virtual void onConnect(const Ble::Address &addr) override;

    /**
     * @copydoc Interface::IBle::StatusCallback::onDisconnect()
     */
    virtual void onDisconnect(uint8_t reason) override;

    /**
     * @copydoc Interface::IBle::CharacteristicCallback::onNotifyIndicate()
     */
    virtual void onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
            bool notify, bool indicate) override;

    /**
     * @copydoc Interface::IBle::CharacteristicCallback::onWrite()
     */
    virtual void onWrite(Ble::CharacteristicHandle chrHandle,
            const uint8_t *pData, uint16_t len) override;

    /**
     * @copydoc Ble::ClientANCS::Callback::onNotifSource()
     */
    virtual void onNotifSource(const uint8_t *pData, uint16_t len) override;

    /**
     * @copydoc Ble::ClientANCS::Callback::onDataSource()
     */
    virtual void onDataSource(const uint8_t *pData, uint16_t len) override;

    /**
     * @copydoc Ble::ClientANCS::Callback::onReady()
     */
    virtual void onReady() override;


    void thread();

    bool notifHandler(const NotifInfo &notif);

    bool iosFetchAttr(ANCS::AttrInfo *attrs, uint16_t attrCount,
            std::shared_ptr<PhoneNotification> notification);
    PhoneNotification::Category iosGetCategory(ANCS::CategoryID id);

    bool androidFetchAttr(CANS::AttrInfo *attrs, uint16_t attrCount,
            std::shared_ptr<PhoneNotification> notification);
    PhoneNotification::Category androidGetCategory(CANS::CategoryID id);


};

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_SERVICE_CNS_HPP */
