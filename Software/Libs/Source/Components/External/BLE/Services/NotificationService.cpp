/**
 ******************************************************************************
 * @file    NotificationService.cpp
 * @brief   Notification Service.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.SRV.Notification"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "NotificationService.hpp"
#include "Crc/Crc32.hpp"

namespace Ble
{

NotificationService::NotificationService(Interface::IBle &ble) : mBle(ble)
{

    mFlags.init(LOG_MODULE_PRX);
    mInNotifQueue.init(LOG_MODULE_PRX);
    mThread.init(LOG_MODULE_PRX, this, &NotificationService::thread, osPriorityBelowNormal);


    // Enable the ANCS phone-link client and receive its notifications.
    ClientANCS::GetInstance().init();
    ClientANCS::GetInstance().attachANCSCallback(this);


    // Create CANS
    mSrvAndr = mBle.addService(UUID(CANS::kSrvUUID));

    mChrAndrNotify = mBle.addCharacteristic(mSrvAndr,
            UUID(CANS::kChrNotifyUUID),
            CANS::kChrNotifyLen,
            CharProp::WRITE_WO_RESP);
    mBle.attachCharacteristicCallback(mChrAndrNotify, this);

    mChrAndrComRsp = mBle.addCharacteristic(mSrvAndr,
            UUID(CANS::kChrCmdRspUUID),
            ble.getMaxCharacteristicLen(),
            (CharProp::WRITE_WO_RESP | CharProp::NOTIFY));
    mBle.attachCharacteristicCallback(mChrAndrComRsp, this);

    mBle.attachStatusCallback(this);
}

NotificationService::~NotificationService()
{
    mBle.detachStatusCallback(this);
    mBle.attachCharacteristicCallback(mChrAndrComRsp, nullptr);
    mBle.attachCharacteristicCallback(mChrAndrNotify, nullptr);
    ClientANCS::GetInstance().attachANCSCallback(nullptr);

    mThread.deinit();
    mInNotifQueue.deinit();
    mFlags.deinit();
}

void NotificationService::enablePhoneNotification(bool en)
{
    mPhoneNotificationEnabled = en;
}

void NotificationService::attachCallback(Callback *pCallback)
{
    mObserver.add(pCallback);
}

void NotificationService::detachCallback(Callback *pCallback)
{
    mObserver.del(pCallback);
}

bool NotificationService::positiveAction(const std::shared_ptr<PhoneNotification> &notification)
{
    if (notification->platform == PhoneNotification::Platform::IOS) {
        uint16_t len = ANCS::CreateActionRequest(notification->getUid(), ANCS::ActionIDPositive,
                mBuffer, sizeof(mBuffer));
        if (!ClientANCS::GetInstance().writeControlPoint(mBuffer, len)) {
            LOG_ERROR("Failed to send action command\n");
            return false;
        }
    } else if (notification->platform == PhoneNotification::Platform::ANDROID) {
        uint16_t len = CANS::CreateActionRequest(notification->getUid(),
                CANS::COMMAND_ID_EXECUTE_POSITIVE_ACTION, mBuffer, sizeof(mBuffer));
        if (!mBle.setCharacteristicValue(mChrAndrComRsp, mBuffer, len)) {
            LOG_ERROR("Failed to send action command\n");
            return false;
        }
    } else {
        return false;
    }

    return true;
}

bool NotificationService::negativeAction(const std::shared_ptr<PhoneNotification> &notification)
{
    if (notification->platform == PhoneNotification::Platform::IOS) {
        uint16_t len = ANCS::CreateActionRequest(notification->getUid(), ANCS::ActionIDNegative,
                mBuffer, sizeof(mBuffer));
        if (!ClientANCS::GetInstance().writeControlPoint(mBuffer, len)) {
            LOG_ERROR("Failed to send action command\n");
            return false;
        }
    } else if (notification->platform == PhoneNotification::Platform::ANDROID) {
        uint16_t len = CANS::CreateActionRequest(notification->getUid(),
                CANS::COMMAND_ID_EXECUTE_NEGATIVE_ACTION, mBuffer, sizeof(mBuffer));
        if (!mBle.setCharacteristicValue(mChrAndrComRsp, mBuffer, len)) {
            LOG_ERROR("Failed to send action command\n");
            return false;
        }
    } else {
        return false;
    }

    return true;
}

void NotificationService::onConnect(const Ble::Address &addr)
{
    // Fresh link: ANCS is not usable until its discovery finishes (onReady).
    mFlags.clear(Flag::AncsReady | Flag::LinkDown);

    QueueEvent evt { };
    evt.evtID = EventID::EVT_ID_CONNECT;

    mInNotifQueue.push(evt, 1);
}

void NotificationService::onDisconnect(uint8_t reason)
{
    mNotificationEnabled = false;

    // Wake any fetch waiting on readiness so it drops instead of blocking.
    mFlags.clear(Flag::AncsReady);
    mFlags.set(Flag::LinkDown);

    QueueEvent evt { };
    evt.evtID = EventID::EVT_ID_DISCONNECT;

    mInNotifQueue.push(evt, 1);
}

void NotificationService::onReady()
{
    // ANCS Control Point is now usable: release held fetch requests.
    mFlags.clear(Flag::LinkDown);
    mFlags.set(Flag::AncsReady);
}

void NotificationService::onNotifyIndicate(Ble::CharacteristicHandle chrHandle,
        bool notify, bool indicate)
{
    if (chrHandle == mChrAndrComRsp) {
        mNotificationEnabled = notify;
    }
}

void NotificationService::onWrite(Ble::CharacteristicHandle chrHandle,
            const uint8_t *pData, uint16_t len)
{
    if (!mNotificationEnabled || !mPhoneNotificationEnabled) {
        return;
    }

    if (chrHandle == mChrAndrNotify) {

        // Filter
        uint32_t crc = CRC32::calculate(pData, len, 0);
        if (mChrAndrNotifyRxCRC == crc) {
            LOG_DEBUG("Duplication of a command\n");
            return;
        }
        mChrAndrNotifyRxCRC = crc;  // Save CRC

        // 01 00 78 56 34 12 01
        CANS::Notification notif {};
        if (CANS::ParseNotification(pData, len, notif)) {
            LOG_DEBUG("[UID: %u] Protocol: %d, Action: %d, CategoryID: %d\n",
                    notif.uid, notif.protocol, notif.action, notif.category);

            if (notif.protocol > CANS::kProtocolVersion) {
                LOG_ERROR("[UID: %u] Unsupported protocol version %d. Minimum %d\n", notif.uid, notif.protocol, CANS::kProtocolVersion);
                return;
            }

            if (!CANS::IsActionID(notif.action)) {
                LOG_ERROR("[UID: %u] Unsupported  CANS::ActionID %d\n", notif.uid, notif.action);
                return;
            }

            if (!CANS::IsCategoryID(notif.category)) {
                LOG_ERROR("[UID: %u] Unsupported  CANS::CategoryID %d\n", notif.uid, notif.category);
                return;
            }

            QueueEvent evt {};
            evt.evtID = EventID::EVT_ID_NOTIF;

            evt.notification.platform = PhoneNotification::Platform::ANDROID;
            evt.notification.action = notif.action;
            evt.notification.category = notif.category;
            evt.notification.uid = notif.uid;
            mInNotifQueue.push(evt, 1);
        }

    } else if (chrHandle == mChrAndrComRsp) {

        // No duplicate-packet CRC filter here (unlike the notify path): a
        // response streams in chunks, and consecutive identical chunks are
        // legitimate data whenever the message content repeats with a period
        // that divides the chunk size ("hahaha...", "!!!", emoji runs). The
        // parser's own attribute validation catches a genuinely duplicated
        // chunk, and the fetch retry recovers.

        // Base attributes:
        // 03 78 56 34 12 05 0A 00 6D 6F 62 69 6C 65 2E 61 70 70 01 05 00 54 69 74 6C 65 02 08 00 53 75 62 74 69 74 6C 65 07 0F 00 32 30 32 34 30 34 30 38 54 31 30 33 32 31 33 08 02 00 4F 4B 09 05 00 43 6C 65 61 72
        // Body:
        // 03 78 56 34 12 04 04 00 42 6F 64 79
        CANS::AttributeParser::Status status = CANS::AttributeParser::Status::ERROR;

        status = mParserANDROID.appendData(pData, len);
        if (status == CANS::AttributeParser::Status::DONE) {
            mFlags.set(Flag::AttrFetchSuccess);
        } else if (status == CANS::AttributeParser::Status::ERROR) {
            LOG_DEBUG_DUMP(pData, len);
            mFlags.set(Flag::AttrFetchFail);
        } else {
            // Waiting for the next chunk
        }
    }
}

void NotificationService::onNotifSource(const uint8_t *pData, uint16_t len)
{
    if (!mPhoneNotificationEnabled) {
        return;
    }

    ANCS::NotificationSource ns {};
    if (ANCS::ParseNotificationSource(pData, len, ns)) {
        LOG_DEBUG("[UID: %u] EventID: %s, EventFlags: 0x%02X, CategoryID: %s, CategoryCount: %d\n",
                ns.NotificationUID, ns.EventIDS, ns.EventFlags, ns.CategoryIDS, ns.CategoryCount);

        if (!ANCS::IsEventID(ns.EventID)) {
            LOG_ERROR("[UID: %u] Unsupported ANCS::EventID %d\n", ns.NotificationUID, ns.EventID);
            return;
        }

        if (!ANCS::IsCategoryID(ns.CategoryID)) {
            LOG_ERROR("[UID: %u] Unsupported ANCS::CategoryID %d\n", ns.NotificationUID, ns.CategoryID);
            return;
        }

        QueueEvent evt {};
        evt.evtID = EventID::EVT_ID_NOTIF;

        evt.notification.platform = PhoneNotification::Platform::IOS;
        evt.notification.action = ns.EventID;
        evt.notification.category = ns.CategoryID;
        evt.notification.uid = ns.NotificationUID;

        mInNotifQueue.push(evt, 1);
    }
}

void NotificationService::onDataSource(const uint8_t *pData, uint16_t len)
{
    ANCS::AttributeParser::Status status = ANCS::AttributeParser::Status::ERROR;

    status = mParserIOS.appendData(pData, len);

    if (status == ANCS::AttributeParser::Status::DONE) {
        mFlags.set(Flag::AttrFetchSuccess);
    } else if (status == ANCS::AttributeParser::Status::ERROR) {
        mFlags.set(Flag::AttrFetchFail);
    } else {
        // Waiting for the next chunk
    }
}

void NotificationService::thread()
{
    QueueEvent evt;

    for (;;) {
        mInNotifQueue.pop(evt);

        switch (evt.evtID) {
            case EventID::EVT_ID_NOTIF: {
                const NotifInfo &n = evt.notification;
                bool needsFetch =
                        (n.platform == PhoneNotification::Platform::IOS) &&
                        (n.action != ANCS::EventID::EventIDNotificationRemoved);

                // ANCS Control Point fetches are impossible until link-client
                // discovery finishes. Wait quietly for readiness rather than
                // firing doomed requests (best-effort: a stale notification is
                // skipped as old once we proceed).
                if (needsFetch && !mFlags.isSet(Flag::AncsReady)) {
                    LOG_DEBUG("[UID: %u] Waiting for ANCS ready\n", n.uid);
                    mFlags.waitAnyNoClear(Flag::AncsReady | Flag::LinkDown,
                            skAncsReadyWaitMs);
                    if (!mFlags.isSet(Flag::AncsReady)) {
                        // Link dropped, or still not ready after the bound: drop.
                        break;
                    }
                }

                if (evt.repeatCnt > 0) {
                    LOG_DEBUG("[UID: %u] Repeat request %u\n", n.uid, evt.repeatCnt);
                }
                bool processed = notifHandler(n);
                if (!processed && evt.repeatCnt < 3) {
                    evt.repeatCnt++;
                    mInNotifQueue.push(evt, 1);
                    // Escalating backoff (200/400/800 ms) so a struggling link
                    // is not hammered with immediate re-requests.
                    OS::Delay(100u << evt.repeatCnt);
                }
            }
                break;
            case EventID::EVT_ID_CONNECT:
                break;
            case EventID::EVT_ID_DISCONNECT:
                mChrAndrNotifyRxCRC = 0;
                mObserver.notify(&NotificationService::Callback::onNotificationRemoveAll);
                break;
            default:
                LOG_ERROR("Unhandled event ID %d\n", evt.evtID);
                break;
        }

    }

}

bool NotificationService::notifHandler(const NotifInfo &notif)
{
    LOG_DEBUG("[UID: %u] Request data\n", notif.uid);

    auto notification = std::make_shared<PhoneNotification>();
    if (!notification) {
        LOG_ERROR("[UID: %u] Unable to allocate memory\n", notif.uid);
        return false;
    }

    notification->rxTimeMs = OS::GetTimeMs();
    notification->platform = notif.platform;
    notification->uid = notif.uid;

    bool isNewNotification = false;

    if (notif.platform == PhoneNotification::Platform::IOS) {

        if (notif.action == ANCS::EventID::EventIDNotificationRemoved) {
            mObserver.notify(&NotificationService::Callback::onNotificationRemove, notif.uid);
            return true;
        } else if (notif.action == ANCS::EventID::EventIDNotificationAdded) {
            isNewNotification = true;
        }

        notification->category = iosGetCategory(static_cast<ANCS::CategoryID>(notif.category));

        std::array<ANCS::AttrInfo, 6> baseAttrList =
        {{
            {ANCS::NotificationAttributeIDAppIdentifier, 32, {}},
            {ANCS::NotificationAttributeIDTitle, 255, {}},
            {ANCS::NotificationAttributeIDSubtitle, 255, {}},
            {ANCS::NotificationAttributeIDDate, 16, {}},
            {ANCS::NotificationAttributeIDPositiveActionLabel, 16, {}},
            {ANCS::NotificationAttributeIDNegativeActionLabel, 16, {}}
        }};

        // Request base attributes
        if (iosFetchAttr(baseAttrList.data(), baseAttrList.size(),
                notification))
        {
            // Request message body
            std::array<ANCS::AttrInfo, 1> bodyAttrList =
            {{
              {ANCS::NotificationAttributeIDMessage,  sizeof(mBuffer) - 8, {}}
            }};

            if (!iosFetchAttr(bodyAttrList.data(), bodyAttrList.size(),
                    notification))
            {
                LOG_ERROR("[UID: %u] Unable to retrieve message content\n", notif.uid);
                return false;
            }
        } else {
            return false;
        }

    } else if (notif.platform == PhoneNotification::Platform::ANDROID) {

        if (notif.action == CANS::ActionID::ACTION_ID_NOTIFICATION_REMOVED) {
            mObserver.notify(&NotificationService::Callback::onNotificationRemove,
                    notif.uid);
            return true;
        } else if (notif.action == CANS::ActionID::ACTION_ID_NOTIFICATION_NEW) {
            isNewNotification = true;
        }

        notification->category = androidGetCategory(static_cast<CANS::CategoryID>(notif.category));

        std::array<CANS::AttrInfo, 6> baseAttrList =
        {{
            {CANS::AttributeID::ATTRIBUTE_ID_APP_ID, 32, {}},
            {CANS::AttributeID::ATTRIBUTE_ID_TITLE, 255, {}},
            {CANS::AttributeID::ATTRIBUTE_ID_SUBTITLE, 255, {}},
            {CANS::AttributeID::ATTRIBUTE_ID_TIMESTAMP, 16, {}},
            {CANS::AttributeID::ATTRIBUTE_ID_POS_ACTION_LABLE, 16, {}},
            {CANS::AttributeID::ATTRIBUTE_ID_NEG_ACTION_LABLE, 16, {}},
        }};

        // Request base attributes
        if (androidFetchAttr(baseAttrList.data(), baseAttrList.size(),
                notification))
        {
            // Request message body
            std::array<CANS::AttrInfo, 1> bodyAttrList =
            {{
              {CANS::AttributeID::ATTRIBUTE_ID_MSG,  sizeof(mBuffer) - 8, {}}
            }};

            if (!androidFetchAttr(bodyAttrList.data(), bodyAttrList.size(),
                    notification))
            {
                LOG_ERROR("[UID: %u] Unable to retrieve message content\n", notif.uid);
                return false;
            }
        } else {
            return false;
        }
    } else {
        // Skip
    }

    // Skip old notifications
    std::tm notifTime = notification->getTimestamp(); // local time

    std::tm localTime {};
    std::time_t utc = std::time(nullptr);
    localtime_r(&utc, &localTime);

    time_t nt = mktime(&notifTime);
    time_t lt = mktime(&localTime);

    if (lt > nt && (lt - nt) > 120) {
        LOG_WARNING("[UID: %u] Skip old notification at %u, now %u\n",
                notif.uid, (uint32_t)nt, (uint32_t)lt);
        return true;
    }

    // Execute callback
    mObserver.notify(&NotificationService::Callback::onNotificationAdd,
            notification, isNewNotification);
    return true;
}

bool NotificationService::iosFetchAttr(ANCS::AttrInfo *attrs,
        uint16_t attrCount, std::shared_ptr<PhoneNotification> notification)
{
    uint16_t mtu = mBle.getMaxCharacteristicLen();

    uint16_t len = ANCS::CreateAttributeRequest(notification->uid, attrs,
            attrCount, mBuffer, sizeof(mBuffer));

    if (len > mtu) {
        LOG_ERROR("[UID: %u] Attribute request not fit into BLE packet\n", notification->uid);
        return false;
    }

    mParserIOS.init(notification->uid, attrs, attrCount, mBuffer, sizeof(mBuffer));
    mFlags.clear(Flag::AttrFetchSuccess | Flag::AttrFetchFail);

    if (!ClientANCS::GetInstance().writeControlPoint(mBuffer, len)) {
        // Transient: the control point is momentarily unusable (e.g. a runtime
        // write racing another client's setup right after discovery). The caller
        // retries, so this is not an error.
        LOG_DEBUG("[UID: %u] Control point busy, will retry\n", notification->uid);
        mParserIOS.reset();
        return false;
    }

    if (!mFlags.waitAnyNoClear(Flag::AttrFetchSuccess | Flag::AttrFetchFail, skTimeout) ||
            !mFlags.isSet(Flag::AttrFetchSuccess))
    {
        LOG_ERROR("[UID: %u] Fetching attributes failed\n", notification->uid);
        // Disarm the parser: it points at this call's stack-allocated attr
        // list, and a response arriving after we give up must not be parsed
        // into it.
        mParserIOS.reset();
        return false;
    }

    for (uint32_t i = 0; i < attrCount; i++) {
        ANCS::AttrInfo &attr = attrs[i];
        switch (attr.id) {
            case ANCS::NotificationAttributeIDAppIdentifier:
                notification->appId = attr.strv;
                break;
            case ANCS::NotificationAttributeIDTitle:
                notification->title = attr.strv;
                break;
            case ANCS::NotificationAttributeIDSubtitle:
                notification->subTitle = attr.strv;
                break;
            case ANCS::NotificationAttributeIDMessage:
                notification->msg = attr.strv;
                break;
            case ANCS::NotificationAttributeIDMessageSize:
                break;
            case ANCS::NotificationAttributeIDDate:
                notification->timestamp = ANCS::ConvertDateToTm(attr.strv.data(),
                        attr.strv.size());
                break;
            case ANCS::NotificationAttributeIDPositiveActionLabel:
                notification->positiveAction = attr.strv;
                break;
            case ANCS::NotificationAttributeIDNegativeActionLabel:
                notification->negativeAction = attr.strv;
                break;
            default:
                break;
        }
    }

    mParserIOS.reset();
    return true;
}

PhoneNotification::Category NotificationService::iosGetCategory(ANCS::CategoryID id)
{
    switch(id) {

        case ANCS::CategoryIDEmail:
            return PhoneNotification::Category::EMAIL;

        case ANCS::CategoryIDIncomingCall:
            return PhoneNotification::Category::INCOMING_CALL;

        case ANCS::CategoryIDMissedCall:
            return PhoneNotification::Category::MISSED_CALL;

        case ANCS::CategoryIDActiveCall:
                    return PhoneNotification::Category::ACTIVE_CALL;

        case ANCS::CategoryIDOther:
        case ANCS::CategoryIDSocial:
        case ANCS::CategoryIDSchedule:
        case ANCS::CategoryIDNews:
        case ANCS::CategoryIDHealthAndFitness:
        case ANCS::CategoryIDBusinessAndFinance:
        case ANCS::CategoryIDEntertainment:
        case ANCS::CategoryIDVoicemail:
        case ANCS::CategoryIDLocation:
        default:
            return PhoneNotification::Category::MESSAGE;
    }
}

bool NotificationService::androidFetchAttr(CANS::AttrInfo *attrs,
        uint16_t attrCount, std::shared_ptr<PhoneNotification> notification)
{
    uint16_t mtu = mBle.getMaxCharacteristicLen();

    uint16_t len = CANS::CreateAttributeRequest(notification->uid, attrs,
            attrCount, mBuffer, sizeof(mBuffer));

    if (len > mtu) {
        LOG_ERROR("[UID: %u] Attribute request not fit into BLE packet\n", notification->uid);
        return false;
    }

    mParserANDROID.init(notification->uid, attrs, attrCount, mBuffer,
            sizeof(mBuffer));
    mFlags.clear(Flag::AttrFetchSuccess | Flag::AttrFetchFail);

    LOG_DEBUG_DUMP(mBuffer, len);
    if (!mBle.setCharacteristicValue(mChrAndrComRsp, mBuffer, len)) {
        LOG_ERROR("[UID: %u] Failed to request attributes\n", notification->uid);
        mParserANDROID.reset();
        return false;
    }

    if (!mFlags.waitAnyNoClear(Flag::AttrFetchSuccess | Flag::AttrFetchFail, skTimeout) ||
            !mFlags.isSet(Flag::AttrFetchSuccess))
    {
        LOG_ERROR("[UID: %u] Fetching attributes failed\n", notification->uid);
        mParserANDROID.reset();
        return false;
    }

    for (uint32_t i = 0; i < attrCount; i++) {
        CANS::AttrInfo &attr = attrs[i];
        switch (attr.id) {

            case CANS::ATTRIBUTE_ID_TITLE:
                notification->title = attr.strv;
                break;
            case CANS::ATTRIBUTE_ID_SUBTITLE:
                notification->subTitle = attr.strv;
                break;
            case CANS::ATTRIBUTE_ID_MSG_CONTENT_SIZE:
                break;
            case CANS::ATTRIBUTE_ID_MSG:
                notification->msg = attr.strv;
                break;
            case CANS::ATTRIBUTE_ID_APP_ID:
                notification->appId = attr.strv;
                break;
            case CANS::ATTRIBUTE_ID_APP_NAME:
                break;
            case CANS::ATTRIBUTE_ID_TIMESTAMP:
                notification->timestamp = CANS::ConvertDateToTm(attr.strv.data(),
                        attr.strv.size());
                break;
            case CANS::ATTRIBUTE_ID_POS_ACTION_LABLE:
                notification->positiveAction = attr.strv;
                break;
            case CANS::ATTRIBUTE_ID_NEG_ACTION_LABLE:
                notification->negativeAction = attr.strv;
                break;
            default:
                break;
        }
    }

    mParserANDROID.reset();
    return true;
}

PhoneNotification::Category NotificationService::androidGetCategory(CANS::CategoryID id)
{
    switch(id) {


        case CANS::CATEGORY_ID_EMAIL:
            return PhoneNotification::Category::EMAIL;

        case CANS::CATEGORY_ID_CALL:
            return PhoneNotification::Category::INCOMING_CALL;

        case CANS::CATEGORY_ID_MISSED_CALL:
            return PhoneNotification::Category::MISSED_CALL;

        case CANS::CATEGORY_ID_ACTIVE_CALL:
            return PhoneNotification::Category::ACTIVE_CALL;

        case CANS::CATEGORY_ID_MESSAGE:
        case CANS::CATEGORY_ID_UNSPECIFIED:
        default:
            return PhoneNotification::Category::MESSAGE;
    }
}


}  /* namespace Ble */
