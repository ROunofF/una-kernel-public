/**
 ******************************************************************************
 * @file    ParserANCS.hpp
 * @brief   Data parser for The Apple Notification Center Service (ANCS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_PARSER_ANCS_HPP
#define __COMPONENT_BLE_PARSER_ANCS_HPP

#include <cstdint>
#include <ctime>
#include <string_view>

namespace Ble {

namespace ANCS {

// EventID values
enum EventID {
   EventIDNotificationAdded     = 0,
   EventIDNotificationModified  = 1,
   EventIDNotificationRemoved   = 2,
   //Reserved EventID values = 3–255
};

/**
 * @brief   Checks if a given ID corresponds to a valid EventID.
 * @param   id: The ID to validate.
 * @return  True if the ID is a valid EventID, otherwise false.
 */
constexpr bool IsEventID(uint8_t id)
{
    return (id == EventIDNotificationAdded ||
            id == EventIDNotificationModified ||
            id == EventIDNotificationRemoved);
}

// EventFlags
enum EventFlags {
   EventFlagSilent              = (1 << 0),
   EventFlagImportant           = (1 << 1),
   EventFlagPreExisting         = (1 << 2),
   EventFlagPositiveAction      = (1 << 3),
   EventFlagNegativeAction      = (1 << 4),
   //Reserved EventFlags   = (1 << 5)–(1 << 7
};

// CategoryID values
enum CategoryID {
   CategoryIDOther              = 0,
   CategoryIDIncomingCall       = 1,
   CategoryIDMissedCall         = 2,
   CategoryIDVoicemail          = 3,
   CategoryIDSocial             = 4,
   CategoryIDSchedule           = 5,
   CategoryIDEmail              = 6,
   CategoryIDNews               = 7,
   CategoryIDHealthAndFitness   = 8,
   CategoryIDBusinessAndFinance = 9,
   CategoryIDLocation           = 10,
   CategoryIDEntertainment      = 11,
   CategoryIDActiveCall         = 12,
   //Reserved CategoryID values   = 13–255
};

/**
 * @brief   Checks if a given ID corresponds to a valid CategoryIDOther.
 * @param   id: The ID to validate.
 * @return  True if the ID is a valid CategoryIDOther, otherwise false.
 */
constexpr bool IsCategoryID(uint8_t id)
{
    return (id == CategoryIDOther ||
            id == CategoryIDIncomingCall ||
            id == CategoryIDMissedCall ||
            id == CategoryIDVoicemail ||
            id == CategoryIDSocial ||
            id == CategoryIDSchedule ||
            id == CategoryIDEmail ||
            id == CategoryIDNews ||
            id == CategoryIDHealthAndFitness ||
            id == CategoryIDBusinessAndFinance ||
            id == CategoryIDLocation ||
            id == CategoryIDEntertainment ||
            id == CategoryIDActiveCall);
}


// CommandID values
enum CommandID{
   CommandIDGetNotificationAttributes = 0,
   CommandIDGetAppAttributes          = 1,
   CommandIDPerformNotificationAction = 2,
   //Reserved CommandID values = 3–255
};

// NotificationAttributeID
enum NotificationAttributeID {
   NotificationAttributeIDAppIdentifier       = 0,
   NotificationAttributeIDTitle               = 1, //(Needs to be followed by a 2-bytes max length parameter)
   NotificationAttributeIDSubtitle            = 2, //(Needs to be followed by a 2-bytes max length parameter)
   NotificationAttributeIDMessage             = 3, //(Needs to be followed by a 2-bytes max length parameter)
   NotificationAttributeIDMessageSize         = 4,
   NotificationAttributeIDDate                = 5,
   NotificationAttributeIDPositiveActionLabel = 6,
   NotificationAttributeIDNegativeActionLabel = 7,
   //Reserved NotificationAttributeID values = 8–255
} ;

/**
 * @brief   Checks if a given ID corresponds to a valid AttributeID.
 * @param   id: The ID to validate.
 * @return  True if the ID is a valid AttributeID, otherwise false.
 */
constexpr bool IsAttributeID(uint8_t id)
{
    return (id == NotificationAttributeIDAppIdentifier ||
            id == NotificationAttributeIDTitle ||
            id == NotificationAttributeIDSubtitle ||
            id == NotificationAttributeIDMessage ||
            id == NotificationAttributeIDMessageSize ||
            id == NotificationAttributeIDDate ||
            id == NotificationAttributeIDPositiveActionLabel ||
            id == NotificationAttributeIDNegativeActionLabel);
}

/*
Note: The format of the NotificationAttributeIDMessageSize constant is a
string that represents the integral value of the message size. The format
of the NotificationAttributeIDDate constant is a string that uses the
Unicode Technical Standard (UTS) #35 date format pattern yyyyMMdd'T'HHmmSS.
The format of all the other constants in Table 3-5 are UTF-8 strings.
*/

// ActionID values
enum ActionID {
   ActionIDPositive = 0,
   ActionIDNegative = 1,
   //Reserved ActionID values = 2–255
};

// AppAttributeID Values
enum AppAttributeID {
   AppAttributeIDDisplayName = 0,
   //Reserved AppAttributeID values = 1–255
};



/**
 * @brief Converts an EventID to its string representation.
 *
 * This function maps an EventID to its corresponding human-readable string.
 * If the EventID is not recognized, it returns "Unknown EventID".
 *
 * @param EventID The event ID to convert.
 * @return A constant string representing the event ID.
 *
 * @note The EventID values must correspond to the indices of the `EventIDStrings` array.
 */
constexpr const char* EventIDtoString(uint8_t EventID)
{
    constexpr const char* EventIDStrings[] = {
        "New message",        // EventIDNotificationAdded
        "Modified message",   // EventIDNotificationModified
        "Removed message"     // EventIDNotificationRemoved
    };

    if (EventID < sizeof(EventIDStrings) / sizeof(EventIDStrings[0])) {
        return EventIDStrings[EventID];
    }
    return "Unknown EventID";
}

/**
 * @brief Converts a CategoryID to its string representation.
 *
 * This function maps a CategoryID to its corresponding human-readable string.
 * If the CategoryID is not recognized, it returns "Unknown CategoryID".
 *
 * @param CategoryID The category ID to convert.
 * @return A constant string representing the category ID.
 *
 * @note The CategoryID values must correspond to the indices of the `CategoryIDStrings` array.
 */
constexpr const char* CategoryIDtoString(uint8_t CategoryID)
{
    constexpr const char* CategoryIDStrings[] = {
        "Other",             // CategoryIDOther
        "IncomingCall",      // CategoryIDIncomingCall
        "MissedCall",        // CategoryIDMissedCall
        "Voicemail",         // CategoryIDVoicemail
        "Social",            // CategoryIDSocial
        "Schedule",          // CategoryIDSchedule
        "Email",             // CategoryIDEmail
        "News",              // CategoryIDNews
        "HealthAndFitness",  // CategoryIDHealthAndFitness
        "BusinessAndFinance",// CategoryIDBusinessAndFinance
        "Location",          // CategoryIDLocation
        "Entertainment",     // CategoryIDEntertainment
        "ActiveCall"         // CategoryIDActiveCall
    };

    if (CategoryID < sizeof(CategoryIDStrings) / sizeof(CategoryIDStrings[0])) {
        return CategoryIDStrings[CategoryID];
    }
    return "Unknown CategoryID";
}


/**
 * @struct NotificationSource
 * @brief Represents a parsed notification source.
 */
struct NotificationSource {
    uint8_t EventID;            ///< The event ID.
    const char *EventIDS;       ///< String representation of the event ID.
    uint8_t EventFlags;         ///< Flags associated with the event.
    uint8_t CategoryID;         ///< The category ID.
    const char *CategoryIDS;    ///< String representation of the category ID.
    uint8_t CategoryCount;      ///< Number of active notifications in the category.
    uint32_t NotificationUID;   ///< Unique identifier of the notification.
};


/**
 * @struct AttrInfo
 * @brief Represents an attribute to be parsed.
 */
struct AttrInfo {
    uint8_t id;                 ///< ID of the attribute.
    uint16_t maxLen;            ///< Maximum allowed length of the attribute.
    std::string_view strv;      ///< String view to store the attribute value.
};

/**
 * @brief Parses a notification source message.
 *
 * This function processes a single notification source message, extracting information
 * about the notification event, category, and unique identifier.
 *
 * @param message: Pointer to the notification source message.
 * @param len: Length of the message in bytes.
 * @param ns: Reference to a NotificationSource structure to store parsed data.
 * @return True if parsing is successful, false otherwise.
 */
bool ParseNotificationSource(const uint8_t *message, uint16_t len,
        NotificationSource &ns);

/**
 * @brief Creates a request packet for notification attributes.
 *
 * This function constructs a packet to request specific notification attributes.
 * The packet includes a command ID, notification UID, and a list of attributes
 * with their respective maximum lengths.
 *
 * @param uid: The notification UID (unique identifier).
 * @param attrs: Array of attributes to request.
 * @param attrCount: Number of attributes in the array.
 * @param buff: Buffer where the request packet will be constructed.
 * @param buffSize: Size of the provided buffer.
 * @return The total length of the constructed packet, or 0 if an error occurs.
 */
uint16_t CreateAttributeRequest(uint32_t uid, const AttrInfo *attrs,
        uint16_t attrCount, uint8_t *buff, uint16_t buffSize);

/**
 * @brief Creates a request packet to perform an action on a notification.
 *
 * This function constructs a request packet to execute a specific action on
 * a notification. The packet includes the action command ID, the unique
 * notification UID, and other required fields to comply with the ANCS protocol
 * specifications.
 *
 * @param uid: The notification UID (unique identifier) to which the action will
 *        be applied.
 * @param action: The action to perform (e.g., Accept, Dismiss).
 * @param buff: Buffer where the request packet will be constructed.
 * @param buffSize: Size of the provided buffer.
 * @return The total length of the constructed packet, or 0 if an error occurs.
 */
uint16_t CreateActionRequest(uint32_t uid, ActionID action, uint8_t *buff,
        uint16_t buffSize);

/**
 * @brief Converts a date string in the format yyyyMMdd'T'HHmmSS to a tm structure.
 * @param dateStr: The input date string.
 * @param len: String length (must be exactly 15 characters long).
 * @return A tm structure representing the parsed date and time. If parsing fails,
 * returns a tm set to 0 UTC.
*/
std::tm ConvertDateToTm(const char* dateStr, size_t len);

/**
 * @class AttributeParser
 * @brief A utility class for parsing notification attributes received in chunks.
 *
 * The class is designed to handle data parsing "on the fly" as it arrives,
 * without requiring a large buffer for storing all incoming data. It processes
 * attributes specified in an initialization array and fills corresponding buffers.
 */
class AttributeParser {
public:

    /**
     * @brief Parsing status.
     */
    enum class Status {
        ERROR,  ///< Any parsing error.
        OK,     ///< Data chunk correctly processed.
        DONE    ///< All attributes parsed.
    };

    AttributeParser() = default;
    virtual ~AttributeParser() = default;

    /**
     * Initialize the parser with necessary data.
     *
     * @param uid: Notification UID.
     * @param attrs: Array of attributes to parse.
     * @param attrCount: Number of attributes in the array.
     * @param workBuffer: Pointer to the external work buffer.
     * @param buffLength: Size of the work buffer.
     */
    void init(uint32_t uid, AttrInfo *attrs, uint16_t attrCount,
            uint8_t *workBuffer, uint16_t buffLength);

    /**
     * @brief Append a chunk of data to the parser for processing.
     *
     * @param data Pointer to the data chunk.
     * @param length Length of the data chunk.
     * @return Parsing status: OK, DONE, or ERROR.
     */
    Status appendData(const uint8_t *data, uint16_t length);

    /**
     * @brief Disarm the parser: drop buffered data, parse progress and the
     *        init context (expected UID, attribute list, work buffer).
     *
     * Must be called when a fetch ends (success or failure) so a late
     * response cannot be parsed into an attribute list that no longer exists.
     */
    void reset();

private:
    uint32_t mExpectedUid = 0;      ///< Expected UID of the notification
    AttrInfo* mAttrs = nullptr;     ///< Array of attribute descriptors
    uint16_t mAttrCount = 0;        ///< Number of attributes

    uint8_t* mBuffer = nullptr;     ///< Working buffer for incoming data
    uint16_t mBufferSize = 0;       ///< Total buffer size
    uint16_t mBufferUsed = 0;       ///< Amount of data currently in the buffer
    uint16_t mCurrentOffset = 0;    ///< Offset of the unprocessed data

    bool mHasParsedHeader = false;  ///< Whether the CommandID and UID have been parsed

    AttrInfo* mCurrentAttr = nullptr;       ///< Current attribute being processed
    bool mCurrentAttrLengthParsed = false;  ///< Whether the current attribute length has been parsed
    uint16_t mCurrentAttrLength = 0;        ///< Length of the current attribute
    uint16_t mCurrentAttrDataReceived = 0;  ///< Amount of data received for the current attribute
    uint16_t mCurrentAttrIndex = 0;         ///< Index of the current attribute being parsed.


    /**
     * @brief Reset the internal parser state.
     */
    void resetState();

    /**
     * @brief Parse newly received data.
     * @return Parsing status: OK, DONE, or ERROR.
     */
    Status parseBuffer();

};

}  /* namespace ANCS */

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_PARSER_ANCS_HPP */
