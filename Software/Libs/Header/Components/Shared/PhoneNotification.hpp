/**
 ******************************************************************************
 * @file    PhoneNotification.hpp
 * @brief   Mobile phone notification object.
 ******************************************************************************
 * This file contains the definition of the "PhoneNotification" structure, which
 * represents a notification object received from a mobile application.
 ******************************************************************************
 */

#ifndef __PHONE_NOTIFICATION_HPP
#define __PHONE_NOTIFICATION_HPP

#include <string>
#include <ctime>

namespace Ble
{
class NotificationService; // Forward declaration
}

/**
 * @brief Mobile application notification object.
 * @note All strings it UTF-8 format.
 */
class PhoneNotification {

public:
    // CategoryID values
    enum class Category {
       UNSPECIFIED   = 0,
       MESSAGE       = 1,
       EMAIL         = 2,
       INCOMING_CALL = 3,
       MISSED_CALL   = 4,
       ACTIVE_CALL   = 5,
    };

    // Platform from which the notification was received
    enum class Platform {
        IOS     = 0,
        ANDROID = 1,
    };

    /**
     * @brief   Constructor.
     */
    PhoneNotification() = default;

    /**
     * @brief   Constructor.
     */
    PhoneNotification(uint32_t rxms, Platform platform, uint32_t uid,
            Category category, const std::tm &timestamp, const char *title,
            const char *subTitle, const char *msg, const char *appId,
            const char *positiveAction, const char *negativeAction) :
            rxTimeMs(rxms), platform(platform), uid(uid), category(category),
            timestamp(timestamp), title(title), subTitle(subTitle), msg(msg),
            appId(appId), positiveAction(positiveAction),
            negativeAction(negativeAction)
    {
    }

    /**
     * @brief   Destructor.
     */
    virtual ~PhoneNotification() = default;

    uint32_t getRxTimeMs() const { return rxTimeMs; }
    Platform getPlatform() const { return platform; }

    uint32_t getUid() const { return uid; }
    Category getCategory() const { return category; }
    std::tm getTimestamp() const { return timestamp; }
    const std::string& getTitle() const { return title; }
    const std::string& getSubTitle() const { return subTitle; }
    const std::string& getMsg() const { return msg; }
    const std::string& getAppId() const { return appId; }
    const std::string& getPositiveAction() const { return positiveAction; }
    const std::string& getNegativeAction() const { return negativeAction; }

    void markAsShown() { shown = true; }
    bool isShown() { return shown; }
    void markAsRead() { read = true; }
    bool isRead() { return read; }

    /**
     * @brief Equality operator to compare two notifications.
     * @param other The other PhoneNotification object to compare.
     * @return 'true' if the notifications are equal, 'false' otherwise.
     *
     * Equality is identity, not content: the unique identifier ('uid') and
     * platform (iOS/Android). A modified notification compares equal to the
     * entry it updates, so consumers can replace it in place. 'rxTimeMs' must
     * stay out of this comparison — it is re-stamped on every fetch, and
     * including it made every update look like a distinct notification
     * (unbounded list growth, see hasSameContent() for content comparison).
     */
    bool operator==(const PhoneNotification &other) const
    {
        return uid == other.uid && platform == other.platform;
    }

    /**
     * @brief Compares the user-visible content of two notifications.
     * @param other The other PhoneNotification object to compare.
     * @return 'true' if everything the GUI renders from is identical.
     *
     * Used to distinguish a meaningful update (new message in a conversation,
     * an incoming call turning into a missed call) from a cosmetic re-post of
     * the same content. The category and action labels are part of the
     * comparison because the popup layout and its buttons are driven by them,
     * not only by the text. The timestamp is deliberately excluded: phones
     * re-stamp it on re-posts, so including it would make every re-post look
     * like a real update; the cost is that an identically-worded re-post
     * keeps showing its original time.
     */
    bool hasSameContent(const PhoneNotification &other) const
    {
        return category == other.category && title == other.title &&
                subTitle == other.subTitle && msg == other.msg &&
                positiveAction == other.positiveAction &&
                negativeAction == other.negativeAction;
    }

    /**
     * @brief Inequality operator to compare two notifications.
     * @param other The other PhoneNotification object to compare.
     * @return 'true' if the notifications are not equal, 'false' otherwise.
     */
    bool operator!=(const PhoneNotification &other) const
    {
        return !(*this == other);
    }

    /**
     * @brief Assignment operator to copy data from one PhoneNotification to another.
     * @param other The other PhoneNotification object to copy data from.
     * @return Reference to the current object after assignment.
     *
     * This operator performs a deep copy of all fields, ensuring the current object
     * contains the same data as the 'other' object.
     */
    PhoneNotification& operator=(const PhoneNotification &other)
    {
        if (this != &other) {
            rxTimeMs = other.rxTimeMs;
            platform = other.platform;
            uid = other.uid;
            category = other.category;
            timestamp = other.timestamp;
            title = other.title;
            subTitle = other.subTitle;
            msg = other.msg;
            appId = other.appId;
            positiveAction = other.positiveAction;
            negativeAction = other.negativeAction;
            shown = other.shown;
            read = other.read;
        }
        return *this;
    }
protected:
    uint32_t rxTimeMs = 0;      ///< System time of receiving notification by the watch in milliseconds.
    Platform platform {};       ///< Platform from which the notification was received (iOS/Android)
    uint32_t uid = 0;           ///< Unique identifier for the notification.
    Category category {};       ///< Notification category.
    std::tm timestamp {};       ///< Local Timestamp indicating when the notification was created.
    std::string title;          ///< Title of the notification.
    std::string subTitle;       ///< Subtitle providing additional context for the notification.
    std::string msg;            ///< Main content or message of the notification.
    std::string appId;          ///< Identifier for the application associated with the notification.
    std::string positiveAction; ///< Action to perform for a positive response (e.g., "Accept").
    std::string negativeAction; ///< Action to perform for a negative response (e.g., "Decline").

    bool shown = false;         ///< Flag to mark notification as shown.
    bool read = false;          ///< Flag to mark notification as read.

    friend class Ble::NotificationService;  // Class that creates PhoneNotification is a friend

};

#endif // __PHONE_NOTIFICATION_HPP
