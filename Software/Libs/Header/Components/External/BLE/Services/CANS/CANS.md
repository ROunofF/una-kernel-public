# BLE Notification Service
Version 1.

This document describes a custom notification protocol designed for seamless interaction between a **Server** (watch) 
and a **Client** (Android smartphone). The protocol supports notification handling, querying additional data, and executing actions 
such as accepting or dismissing notifications.

---

## Overview
- **Server:** Watch
- **Client:** Smartphone (e.g., Android, mobile application)
- **Service UUID:** `554e4100-28e7-4811-0000-141f8b92ee40`
  - **Characteristics:**
      1. **Notification Characteristic** (Write without response): Used for sending notifications.
          - **UUID:** `554e4100-28e7-4811-0001-141f8b92ee40`
      2. **Command/Response Characteristic** (Write without response | Notify): Bidirectional characteristicUsed for requesting additional information or triggering actions.
          - **UUID:** `554e4100-28e7-4811-0002-141f8b92ee40`
- **Endianness:** All numerical values shall be little endian.
- **String Encoding:** All string values shall be composed of Unicode characters encoded in UTF-8. Strings are not null-terminated; instead, their length in bytes is specified.
- **Response timeout:** The timeout is 500ms. If the **Client** does not respond within this time (Request for Attributes), the **Server** can retry the command. If the **Server** receives a corrupted packet, it can retry the command after waiting for the specified timeout.
- **Reserved identifiers:** The use of reserved identifiers should be considered an error.

---

### Notification Characteristic
The **Notification Characteristic** is used to send base notification information from the **Client** (e.g., mobile app) 
to the **Server** (e.g., watch). This notification does not include attributes in order to reduce BLE traffic and filter 
out unnecessary notifications. Typically, the notification UID is added to an internal queue, and the **Server** will request 
additional attributes when needed.

#### Notification Characteristic Format
| **Field**          | **Size (bytes)** | **Description**                                                                           |
|--------------------|------------------|-------------------------------------------------------------------------------------------|
| Protocol Version   | 1                | Protocol version identifier. Current version: `0x01`.                                     |
| Action ID          | 1                | Notification action. Possible values: `0x00` (Add), `0x01` (Remove), and `0x02` (Modify). |
| Notification UID   | 4                | Unique identifier for the notification.                                                   |
| Category           | 1                | Notification category (e.g., Message, Call, Other).                                       |

#### Action ID Values
| **Value** | **Description**                                                           |
|-----------|---------------------------------------------------------------------------|
| `0x00`    | New notification.                                                         |
| `0x01`    | Notification removed.                                                     |
| `0x02`    | Notification modified. The **Server** must re-query attributes if needed. |

---

### Command/Response Characteristic
The **Command Characteristic** is bidirectional and allows the **Server** to request notification attributes, initiate 
actions on notifications, and receive attribute values. 

#### Command/Action ID Values
| **Value**  | **Description**           |
|------------|---------------------------|
| `0x03`     | Request attributes.       |
| `0x04`     | Execute positive action.  |
| `0x05`     | Execute negative action.  |

#### Request Attributes (command `0x03`)
The Type-Length (TL) format is used to request attributes, allowing multiple attributes to be requested at once by 
specifying the maximum data size the **Server** is ready to receive. The response should be in the same characteristic in 
Type-Length-Value (TLV) format. If the attribute length exceeds the packet size (depending on MTU), the data will be 
sent in multiple packets, and the **Server** must assemble fragments until it receives a complete response with all attributes. 
The **Server** should not send a new command until it receives all message fragments or the timeout specified above has elapsed. 
If the **Server** determines that the fragments are damaged or not all were received, it can resend the command after waiting 
for the timeout specified above.

#### Command Characteristic Format for Request Attributes
| **Field**          | **Size (bytes)** | **Description**                                                       |
|--------------------|------------------|-----------------------------------------------------------------------|
| Command ID         | 1                | Identifier for the command (`0x03`).                                  |
| Notification UID   | 4                | Unique identifier for the notification.                               |
| Attribute Requests | Variable         | List of attribute IDs and their maximum lengths in bytes (see below). |

Multiple attribute requests can be included sequentially:
```
[Attribute ID][Max Length][Attribute ID][Max Length]...
```

#### Actions on the notification (commands `0x04` and `0x05`)
These commands initiate a specific action on the message. The action type can be individual for each notification, it 
can be obtained by reading attributes **Positive Action Label** (`0х08`) and **Negative Action Label** (`0x09`)
respectively.

#### Command Characteristic Format for Actions
| **Field**          | **Size (bytes)** | **Description**                               |
|--------------------|------------------|-----------------------------------------------|
| Command ID         | 1                | Identifier for the action (`0x04` or `0x05`). |
| Notification UID   | 4                | Unique identifier for the notification.       |


#### Response Characteristic Format
The **Client's** response is only required for the attribute request command `0x03`.
The response should be in the Type-Length-Value (TLV) format. If the attribute length exceeds the packet size 
(depending on MTU), the data will be sent in multiple packets, and the **Server** must assemble fragments until it receives 
a complete response with all attributes. The length of the attribute sent by the **Client** must not exceed the length 
specified in the request. If it does, the **Client** must truncate the content to the specified length.

| **Field**                         | **Size (bytes)** | **Description**                                                   |
|-----------------------------------|------------------|-------------------------------------------------------------------|
| Requested Command ID / Error code | 1                | Repeats the command identifier number or error code (see below)   |
| Notification UID                  | 4                | Unique identifier for the notification Not included if error.     |
| Attribute Responses               | Variable         | List of attribute data (see format below). Not included if error. |

#### Attribute Response Format
| **Field**    | **Size (bytes)**  | **Description**                             |
|--------------|-------------------|---------------------------------------------|
| Attribute ID | 1                 | Identifier for the requested attribute.     |
| Length       | 2                 | Length of the included data chunk in bytes. |
| Data         | Variable          | Requested data chunk.                       |

Multiple attribute responses can be included sequentially. Data can be fragmented anywhere:
If the requested attribute ID is valid but its value is missing, the **Client** should return an attribute length of 0.
If at least one of the requested attribute IDs is invalid, the **Client** should return the error `0x82` (Invalid Attribute ID).

First fragment:
```
[Attribute ID][Data length][Data][Attribute ID][Data length][Da
```
Second fragment:
```
ta][Attribute ID][Data length][Data][Attribute ID][Data length]...
```
---

## Appendix:
### Notification Categories
| **Category**      | **Value**     | **Description**                     |
|-------------------|---------------|-------------------------------------|
| Other/Unspecified | `0x00`        | Fallback for unsupported types.     |
| Message           | `0x01`        | Notification for messages.          |
| Call              | `0x02`        | Notification for calls.             |
| Reserved          | `0x03 - 0xFF` | Reserved for additional categories. |

### Attribute IDs
| **ID**        | **Attribute**         | **Description**                                     |
|---------------|-----------------------|-----------------------------------------------------|
| `0x01`        | Title                 | Title of the notification.                          |
| `0x02`        | Subtitle              | Subtitle of the notification (if applicable).       |
| `0x03`        | Message content size  | Size of the body text of the notification in bytes. |
| `0x04`        | Message               | Main body text of the notification.                 |
| `0x05`        | App Identifier        | Identifier for the app sending the notification.    |
| `0x06`        | App Name              | Name for the app sending the notification.          |
| `0x07`        | Timestamp             | Timestamp of the notification.                      |
| `0x08`        | Positive Action Label | Label for a positive action (e.g., "Accept").       |
| `0x09`        | Negative Action Label | Label for a negative action (e.g., "Decline").      |
| `0x0A - 0xFF` | Reserved              | Reserved for additional IDs.                        |

### Timestamp format
The format of the Attribute ID **Timestamp** (`0x07`) is ISO 8601. Timestamp is always in local time.
```
yyyyMMdd'T'HHmmss 
```
```
Example: 20240408T103213
```

### Error Codes
| **Code**        | **Description**             |
|-----------------|-----------------------------|
| `0x80`          | Invalid request format.     |
| `0x81`          | Notification UID not found. |
| `0x82`          | Invalid Attribute ID.       |
| `0x83` - `0xFF` | Reserved codes.             |

---

## Communication Examples

### Example 1: Adding a Notification
#### Scenario
The smartphone sends a new notification to the watch.

#### Notification Data
| **Field**            | **Value**               |
|----------------------|-------------------------|
| Protocol Version     | `0x01`                  |
| Action ID            | `0x00` (Add)            |
| Notification UID     | `0x12345678`            |
| Category             | `0x01` (Message)        |

#### Packet Format
```
[Protocol Version][Action ID][Notification UID][Category]
01 00 78 56 34 12 01
```

---

### Example 2: Requesting Notification Attributes
#### Scenario
The watch requests attributes for a notification with UID `0x12345678`.

#### Request Data
| **Field**            | **Value**                       |
|----------------------|---------------------------------|
| Command ID           | `0x03` (Request Attributes)     |
| Notification UID     | `0x12345678`                    |
| Attribute Requests   | `[0x01, 0x0020] [0x04, 0x0040]` |

#### Packet Format
```
[Command ID][Notification UID][Attribute ID][Max Length][Attribute ID][Max Length]
03 78 56 34 12 01 20 00 04 40 00
```

---

### Example 3: Responding with Notification Attributes
#### Scenario
The smartphone responds to the request for attributes.

#### Response Data
| **Field**                | **Value**                   |
|--------------------------|-----------------------------|
| Requested Command ID     | `0x03`                      |
| Notification UID         | `0x12345678`                |
| Attribute Responses      |                             |
| - Attribute ID           | `0x01`                      |
| - Length                 | `0x000C`                    |
| - Data                   | `"Notification"` (UTF-8)    |
| - Attribute ID           | `0x04`                      |
| - Length                 | `0x000F`                    |
| - Data                   | `"Message content"` (UTF-8) |

#### Packet Format
```
[Requested Command ID][Notification UID][Attribute ID][Length][Data][Attribute ID][Length][Data]
03 78 56 34 12 01 0C 00 4E 6F 74 69 66 69 63 61 74 69 6F 6E 04 0F 00 4D 65 73 73 61 67 65 20 63 6F 6E 74 65 6E 74
```

---

### Example 4: Executing Positive Action
#### Scenario
The watch executes a positive action on a notification.

#### Action Data
| **Field**            | **Value**                |
|----------------------|--------------------------|
| Command ID           | `0x04` (Positive Action) |
| Notification UID     | `0x12345678`             |

#### Packet Format
```
[Command ID][Notification UID]
04 78 56 34 12
```

---

### Example 5: Error Response
#### Scenario
The smartphone sends an error response indicating that the requested notification UID was not found.

#### Error Data
| **Field**            | **Value**                           |
|----------------------|-------------------------------------|
| Error Code           | `0x81` (Notification UID not found) |

#### Packet Format
```
[Error Code]
81
```

---

### Example 6: Removing a Notification
#### Scenario
The smartphone informs the watch to remove a notification.

#### Notification Data
| **Field**            | **Value**              |
|----------------------|------------------------|
| Protocol Version     | `0x01`                 |
| Action ID            | `0x01` (Remove)        |
| Notification UID     | `0x12345678`           |
| Category             | `0x00` (Other)         |

#### Packet Format
```
[Protocol Version][Action ID][Notification UID][Category]
01 01 78 56 34 12 00
```

---

### Example 7: Responding with Fragmented Notification Attribute
#### Scenario
The smartphone responds to the request for attributes, where one attribute's data does not fit into a single packet 
and is split into two fragments.

#### Response Data (Fragment 1)
| **Field**               | **Value**                      |
|-------------------------|--------------------------------|
| Requested Command ID    | `0x03`                         |
| Notification UID        | `0x12345678`                   |
| Attribute Responses     |                                |
| - Attribute ID          | `0x02` (Long Data)             |
| - Length                | `0x0023`                       |
| - Data (First Fragment) | `"This is the first "` (UTF-8) |

#### Packet Format (Fragment 1)
```
[Requested Command ID][Notification UID][Attribute ID][Length][Data]
03 78 56 34 12 02 23 00 54 68 69 73 20 69 73 20 74 68 65 20 66 69 72 73 74 20
```

#### Response Data (Fragment 2)
| **Field**                | **Value**                     |
|--------------------------|-------------------------------|
| - Data (Second Fragment) | `"fragment of data."` (UTF-8) |
| - Attribute ID           | `0x01`                        |
| - Length                 | `0x000C`                      |
| - Data                   | `"Notification"` (UTF-8)      |
#### Packet Format (Fragment 2)
```
[Data][Attribute ID][Length][Data]
66 72 61 67 6D 65 6E 74 20 6F 66 20 64 61 74 61 2E 01 0C 00 4E 6F 74 69 66 69 63 61 74 69 6F 6E
```