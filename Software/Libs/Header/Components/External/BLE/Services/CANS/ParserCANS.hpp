/**
 ******************************************************************************
 * @file    ParserCANS.hpp
 * @brief   Data parser for Custom Android Notification Service (CANS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_PARSER_CANS_HPP
#define __COMPONENT_BLE_PARSER_CANS_HPP

#include <cstdint>
#include <ctime>
#include <string_view>

namespace Ble {

namespace CANS {

/**
 * @brief   Protocol version identifier.
 */
constexpr uint8_t kProtocolVersion = 0x01;

/**
 * @brief   Service UUID for the Custom Notification Service (CANS).
 */
constexpr const char *kSrvUUID = "554E4100-28E7-4811-0000-141F8B92EE40";

/**
 * @brief   Characteristic UUID for sending notifications from the server to the client.
 */
constexpr const char *kChrNotifyUUID = "554E4100-28E7-4811-0001-141F8B92EE40";

/**
 * @brief   Length of Notification Characteristic .
 */
constexpr uint32_t kChrNotifyLen = 7;

/**
 * @brief   Characteristic UUID for sending commands and receiving responses.
 */
constexpr const char *kChrCmdRspUUID = "554E4100-28E7-4811-0002-141F8B92EE40";

/**
 * @brief   Action identifiers used in notifications to describe the type of
 *          action being performed.
 */
enum ActionID : uint8_t {
    ACTION_ID_NOTIFICATION_NEW = 0x00,      ///< New notification.
    ACTION_ID_NOTIFICATION_REMOVED = 0x01,  ///< Notification has been removed.
    ACTION_ID_NOTIFICATION_MODIFIED = 0x02  ///< Notification has been modified.
};

/**
 * @brief   Checks if a given ID corresponds to a valid ActionID.
 * @param   id: The ID to validate.
 * @return  True if the ID is a valid ActionID, otherwise false.
 */
constexpr bool IsActionID(uint8_t id)
{
    return (id == ACTION_ID_NOTIFICATION_NEW ||
            id == ACTION_ID_NOTIFICATION_REMOVED ||
            id == ACTION_ID_NOTIFICATION_MODIFIED);
}

/**
 * @brief   Command identifiers for communication between the client and server.
 */
enum CommandID : uint8_t {
    COMMAND_ID_REQUEST_ATTRIBUTES = 0x03,       ///< Request attributes of a notification.
    COMMAND_ID_EXECUTE_POSITIVE_ACTION = 0x04,  ///< Execute a positive action (e.g., "Accept").
    COMMAND_ID_EXECUTE_NEGATIVE_ACTION = 0x05   ///< Execute a negative action (e.g., "Decline").
};

/**
 * @brief   Checks if a given ID corresponds to a valid CommandID.
 * @param   id: The ID to validate.
 * @return  True if the ID is a valid CommandID, otherwise false.
 */
constexpr bool IsCommandID(uint8_t id)
{
    return (id == COMMAND_ID_REQUEST_ATTRIBUTES ||
            id == COMMAND_ID_EXECUTE_POSITIVE_ACTION ||
            id == COMMAND_ID_EXECUTE_NEGATIVE_ACTION);
}

/**
 * @brief   Notification category identifiers to classify the type of notification.
 */
enum CategoryID : uint8_t {
    CATEGORY_ID_UNSPECIFIED = 0x00, ///< Unspecified category.
    CATEGORY_ID_MESSAGE = 0x01,     ///< Message notifications.
    CATEGORY_ID_EMAIL = 0x02,       ///< Email notifications.
    CATEGORY_ID_CALL = 0x03,        ///< Call notifications.
    CATEGORY_ID_MISSED_CALL = 0x04, ///< Missed call notifications.
    CATEGORY_ID_ACTIVE_CALL = 0x05, ///< Active call notifications.
};

/**
 * @brief   Checks if a given ID corresponds to a valid CategoryID.
 * @param   id: The ID to validate.
 * @return  True if the ID is a valid CategoryID, otherwise false.
 */
constexpr bool IsCategoryID(uint8_t id)
{
    return (id == CATEGORY_ID_UNSPECIFIED ||
            id == CATEGORY_ID_MESSAGE ||
            id == CATEGORY_ID_EMAIL ||
            id == CATEGORY_ID_CALL ||
            id == CATEGORY_ID_MISSED_CALL ||
            id == CATEGORY_ID_ACTIVE_CALL);
}

/**
 * @brief   Attribute identifiers for accessing specific data related to a notification.
 */
enum AttributeID : uint8_t {
    ATTRIBUTE_ID_TITLE = 0x01,              ///< Title of the notification.
    ATTRIBUTE_ID_SUBTITLE = 0x02,           ///< Subtitle of the notification (if applicable).
    ATTRIBUTE_ID_MSG_CONTENT_SIZE = 0x03,   ///< Size of the message body in bytes.
    ATTRIBUTE_ID_MSG = 0x04,                ///< Main body of the notification.
    ATTRIBUTE_ID_APP_ID = 0x05,             ///< Identifier of the app sending the notification.
    ATTRIBUTE_ID_APP_NAME = 0x06,           ///< Name of the app sending the notification.
    ATTRIBUTE_ID_TIMESTAMP = 0x07,          ///< Timestamp of the notification.
    ATTRIBUTE_ID_POS_ACTION_LABLE = 0x08,   ///< Label for a positive action (e.g., "Accept").
    ATTRIBUTE_ID_NEG_ACTION_LABLE = 0x09    ///< Label for a negative action (e.g., "Decline").
};

/**
 * @brief   Checks if a given ID corresponds to a valid AttributeID.
 * @param   id: The ID to validate.
 * @return  True if the ID is a valid AttributeID, otherwise false.
 */
constexpr bool IsAttributeID(uint8_t id)
{
    return (id == ATTRIBUTE_ID_TITLE ||
            id == ATTRIBUTE_ID_SUBTITLE ||
            id == ATTRIBUTE_ID_MSG_CONTENT_SIZE ||
            id == ATTRIBUTE_ID_MSG ||
            id == ATTRIBUTE_ID_APP_ID ||
            id == ATTRIBUTE_ID_APP_NAME ||
            id == ATTRIBUTE_ID_TIMESTAMP ||
            id == ATTRIBUTE_ID_POS_ACTION_LABLE ||
            id == ATTRIBUTE_ID_NEG_ACTION_LABLE);
}

/**
 * @brief   Error codes for communication failures or invalid requests.
 */
enum ErrorCode : uint8_t {
    ERROR_CODE_INVALID_REQUEST_FORMAT = 0x80,   ///< The request format is invalid.
    ERROR_CODE_UID_NOT_FOUND = 0x81,            ///< Notification UID not found.
    ERROR_CODE_ATTR_DATA_NOT_AVAILABLE = 0x82, ///< Requested attribute data is unavailable.
};

/**
 * @brief   Checks if a given ID corresponds to a valid ErrorCode.
 * @param   id: The ID to validate.
 * @return  True if the ID is a valid ErrorCode, otherwise false.
 */
constexpr bool IsErrorCode(uint8_t id)
{
    return (id == ERROR_CODE_INVALID_REQUEST_FORMAT ||
            id == ERROR_CODE_UID_NOT_FOUND ||
            id == ERROR_CODE_ATTR_DATA_NOT_AVAILABLE);
}

/**
 * @struct  Notification
 * @brief   Structure representing a notification.
 */
struct Notification {
    uint8_t protocol;   ///< Protocol version used for the notification.
    uint8_t action;     ///< Action ID indicating the type of operation (e.g., new, removed, modified).
    uint32_t uid;       ///< Unique identifier for the notification.
    uint8_t category;   ///< Category ID classifying the notification type.
};

/**
 * @struct  AttrInfo
 * @brief   Represents an attribute to be parsed.
 */
struct AttrInfo {
    uint8_t id;                 ///< ID of the attribute.
    uint16_t maxLen;            ///< Maximum allowed length of the attribute.
    std::string_view strv;      ///< String view to store the attribute value.
};




/**
 * @brief   Parses a notification message received from the client.
 *
 * This function extracts the fields of a notification from the provided
 * message buffer and populates the provided `Notification` structure. It
 * ensures that the message length and format are valid before parsing.
 *
 * @param   message: Pointer to the message buffer containing the notification data.
 * @param   len: Length of the message buffer.
 * @param   ns: Reference to a `Notification` structure to store the parsed data.
 * @return  True if the parsing is successful and the data is valid, otherwise false.
 */
bool ParseNotification(const uint8_t *message, uint16_t len,
        Notification &ns);

/**
 * @brief   Creates a request to retrieve specific attributes of a notification.
 *
 * This function constructs an attribute request message for a specific
 * notification UID, including the list of attributes to retrieve. The
 * constructed message is stored in the provided buffer.
 *
 * @param   uid: The unique identifier of the notification for which attributes
 *          are requested.
 * @param   attrs: Pointer to an array of `AttrInfo` structures representing the
 *          attributes to request.
 * @param   attrCount: Number of attributes in the `attrs` array.
 * @param   buff: Pointer to the buffer where the request message will be written.
 * @param   buffSize: Size of the buffer in bytes.
 * @return  The size of the constructed message in bytes, or 0 if the request
 *          could not be created.
 */
uint16_t CreateAttributeRequest(uint32_t uid, const AttrInfo *attrs,
        uint16_t attrCount, uint8_t *buff, uint16_t buffSize);


/**
 * @brief Creates a request packet to perform an action on a notification.
 *
 * This function constructs a request packet to execute a specific action on
 * a notification. The packet includes the action command ID and the unique
 * notification UID.
 *
 * @param uid: The notification UID (unique identifier) to which the action will
 *        be applied.
 * @param cid: The action to perform (e.g., COMMAND_ID_EXECUTE_POSITIVE_ACTION,
 *        COMMAND_ID_EXECUTE_NEGATIVE_ACTION).
 * @param buff: Buffer where the request packet will be constructed.
 * @param buffSize: Size of the provided buffer.
 * @return The total length of the constructed packet, or 0 if an error occurs.
 */
uint16_t CreateActionRequest(uint32_t uid, CommandID cid, uint8_t *buff,
        uint16_t buffSize);

/**
 * @brief   Converts a date string in the format yyyyMMdd'T'HHmmss to a tm structure.
 * @param   dateStr: The input date string.
 * @param   len: String length (must be exactly 15 characters long).
 * @return  A tm structure representing the parsed date and time. If parsing fails,
 *          returns a tm set to 0 UTC.
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
     * @brief Reset the parser state.
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
     * @brief Drop buffered data and parse progress, keeping the init context
     *        (expected UID, attribute list, work buffer) so the parser stays
     *        armed for the response it is actually waiting for.
     */
    void discardPending();

    /**
     * @brief Parse newly received data.
     * @return Parsing status: OK, DONE, or ERROR.
     */
    Status parseBuffer();

};

} /* namespace CANS */

}  /* namespace Ble */

#endif /* __COMPONENT_BLE_PARSER_CANS_HPP */
