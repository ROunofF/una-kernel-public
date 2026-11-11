# Custom Android Media Service (CAMS)

Version 1.

CAMS lets the UNA Watch and an Android phone exchange media state and control: the
phone reports its "Now Playing" state to the watch, and the watch sends media remote
commands (play / pause / next / ...) back to the phone.

CAMS is the Android counterpart of the iOS Apple Media Service (AMS) path, the way
the Custom Android Notification Service (CANS) is the counterpart of Apple's ANCS.
Despite the name, **CAMS is not a separate GATT service** - the BlueNRG controller
has no free service slots, so CAMS is carried over the existing Custom Communication
Service (CCS) characteristics (see Overview). The "Service" in the name denotes the
logical media capability exposed to Android.

Sections 1-2 specify the over-the-air protocol (the contract between phone and
watch); the examples are in section 3. Internally both CAMS and AMS feed the same
firmware `MediaService`, which renders one media screen and emits one command set.

---

## Overview

- **Server:** Watch
- **Client:** Smartphone (Android)
- **Transport:** the existing **Custom Communication Service (CCS)** -
  `554E4100-A2CF-4DF8-0000-7E1E48595106`. CAMS adds no new GATT service; the phone
  uses the two CCS characteristics it already knows:
  - **Command characteristic** `554E4100-A2CF-4DF8-0001-7E1E48595106`
    (WRITE_NO_RESPONSE | NOTIFY): the phone WRITES Now Playing state here, as a new
    command `0x20`; the watch NOTIFYs its status response on the same characteristic.
  - **Event characteristic** `554E4100-A2CF-4DF8-0002-7E1E48595106` (NOTIFY): the
    watch NOTIFYs remote commands here, as a new event `0x03`.
- **Endianness:** all multi-byte numeric values are little-endian.
- **Strings:** UTF-8, NOT null-terminated; each string's byte length is carried in
  its TLV length field.
- **Notifications:** as with the rest of CCS, the watch ignores writes and sends
  nothing until the client has enabled notifications on both characteristics.

---

## 1. Now Playing update (phone -> watch)

The phone reports the current media state by WRITING command `0x20` to the Command
characteristic. It follows the standard CCS request-response convention: the watch
applies the update and NOTIFYs a 2-byte status back. The phone may ignore the ack
(Now Playing is a one-way state stream); it exists so the watch can flag a malformed
packet and to keep the command consistent with the rest of CCS.

### 1.1 Packet format

An update is **incremental and presence-aware**: it carries only the fields the
player currently has, and a field that is absent from a packet leaves the watch's
last known value unchanged (it is NOT reset to zero).

The CCS two-byte header (command number + reserved `0x00`) is followed by one or
more attribute entries in Type-Length-Value form:

| Field            | Size (bytes) | Description                                  |
|------------------|--------------|----------------------------------------------|
| Command ID       | 1            | Always `0x20` (Media Now Playing).           |
| Reserved         | 1            | Always `0x00`.                               |
| Attribute list   | Variable     | One or more TLV entries (see below).         |

Each attribute entry:

| Field        | Size (bytes) | Description                                  |
|--------------|--------------|----------------------------------------------|
| Attribute ID | 1            | See the Media Attribute IDs table.           |
| Length       | 1            | Length of `Value` in bytes (0..255).         |
| Value        | `Length`     | Encoded per the attribute (string or number).|

```
[0x20][0x00] [AttrId][Len][Value] [AttrId][Len][Value] ...
```

The phone may pack several short attributes into one packet (up to the negotiated
ATT MTU) or send each in its own packet - the watch applies each entry the moment it
arrives.

### 1.2 Media Attribute IDs

| ID     | Attribute      | Value encoding                                  |
|--------|----------------|-------------------------------------------------|
| `0x01` | Title          | UTF-8 string                                    |
| `0x02` | Artist         | UTF-8 string                                    |
| `0x03` | Album          | UTF-8 string                                    |
| `0x04` | Duration       | `uint32` LE, seconds                            |
| `0x05` | Player Name    | UTF-8 string                                    |
| `0x06` | Playback State | `uint8` (see Playback State)                    |
| `0x07` | Elapsed        | `uint32` LE, seconds                            |
| `0x08` | Rate           | `uint16` LE, hundredths (100 = 1.00x)           |
| `0x09` | Volume         | `uint8`, percent 0..100                         |
| `0x0A` | Queue Index    | `uint16` LE                                     |
| `0x0B` | Queue Count    | `uint16` LE                                     |
| `0x0C` | Shuffle Mode   | `uint8` (0 off, 1 one, 2 all)                   |
| `0x0D` | Repeat Mode    | `uint8` (0 off, 1 one, 2 all)                   |
| `0x0E - 0xFF` | Reserved | -                                               |

Unknown attribute IDs are skipped (the TLV length lets the watch advance past them),
so new attributes can be added later without breaking older firmware.

### 1.3 Playback State (attribute `0x06`)

| Value  | State           |
|--------|-----------------|
| `0x00` | Paused          |
| `0x01` | Playing         |
| `0x02` | Rewinding       |
| `0x03` | Fast-forwarding |

"Unknown" is expressed by simply not sending the Playback State attribute.

### 1.4 Update semantics and recommended phone behaviour

**Local progress.** The watch advances Elapsed locally (one second per second while
the state is Playing) and draws the progress bar from that, so the phone does **not**
need to stream Elapsed every second. Each Elapsed the phone sends re-synchronises the
watch's local counter to the true position; each Playback State the phone sends sets
play/pause, which freezes or resumes that local advance.

**Player switch.** Sending a Player Name different from the current one tells the
watch a new player took over; the watch clears all previously known fields before
applying the rest of that packet. So when the player changes, include the new
player's fields in (or right after) the same update.

**What to send and when:**

- **Track change:** a full snapshot - Title, Artist, Album, Duration, Player Name,
  Playback State, Elapsed (usually 0). Split across several `0x20` writes if it
  exceeds the MTU.
- **On every play / pause / seek:** Playback State together with the current Elapsed.
  Sending them in one packet lets the bar jump to the true position and update
  play/pause at once.
- **While playing:** re-send Elapsed (with Playback State) occasionally to correct
  drift - about once every 5 seconds is plenty.
- **Queue Index/Count:** optional. The watch does not display the queue yet; send
  them only if convenient (the attributes are reserved for a future screen).
- **Volume:** optional. The watch does not display volume yet, but the Volume
  attribute (`0x09`) and the Volume Up/Down remote commands (`0x05`/`0x06`) are part
  of the protocol so volume can be added to the screen later without a protocol
  change.

### 1.5 Response

After processing a `0x20` packet the watch NOTIFYs a 2-byte status on the Command
characteristic (CCS response header = command number + execution status):

| Field      | Size (bytes) | Description                                  |
|------------|--------------|----------------------------------------------|
| Command ID | 1            | Always `0x20`.                               |
| Status     | 1            | `0x01` OK, `0x02` error (malformed TLV).     |

```
[0x20][status]
```

The watch applies every well-formed attribute it can parse; `0x02` means a TLV length
ran past the end of the packet.

### 1.6 Fragmentation

There is **no cross-packet reassembly**. Each `0x20` write is a self-contained
incremental update, and the whole write MUST fit in one ATT operation: at most
`ATT_MTU - 3` bytes, and never more than the Command characteristic length of
**220 bytes** (the watch's GATT server rejects a longer write). Account for the
2-byte header and the 2-byte (id + length) overhead per attribute. If a string is
longer than will fit, truncate it on a UTF-8 character boundary; the watch renders
what it receives.

---

## 2. Remote command (watch -> phone)

When the user operates the watch's media screen, the watch NOTIFYs a 2-byte event on
the Event characteristic.

### 2.1 Notification format

| Field      | Size (bytes) | Description                                  |
|------------|--------------|----------------------------------------------|
| Event ID   | 1            | Always `0x03` (Media Remote Command).        |
| Command    | 1            | A Media Command value (see below).           |

```
[0x03][Command]
```

### 2.2 Media Command values

| Value  | Command           |
|--------|-------------------|
| `0x00` | Play              |
| `0x01` | Pause             |
| `0x02` | Toggle Play/Pause |
| `0x03` | Next Track        |
| `0x04` | Previous Track    |
| `0x05` | Volume Up         |
| `0x06` | Volume Down       |

Values `0x07+` are reserved for later (repeat/shuffle/skip/like/...). A future
absolute-volume or seek command can append a parameter after the Command byte without
breaking this format.

---

## 3. Examples

### 3.1 Track change (one packet)

Title "Song", Artist "Band", Duration 240 s, Player "Music", State Playing,
Elapsed 0:

```
[CmdId][rsv] [0x01 04 "Song"] [0x02 04 "Band"] [0x04 04 F0 00 00 00] [0x05 05 "Music"] [0x06 01 01] [0x07 04 00 00 00 00]
 20    00     01 04 53 6F 6E 67  02 04 42 61 6E 64  04 04 F0 00 00 00  05 05 4D 75 73 69 63  06 01 01  07 04 00 00 00 00
```

Watch response: `20 01` (OK).

### 3.2 Periodic progress (Elapsed 65 s, still Playing)

```
[CmdId][rsv] [0x07 04 41 00 00 00] [0x06 01 01]
 20    00     07 04 41 00 00 00     06 01 01
```

### 3.3 Pause

```
[CmdId][rsv] [0x06 01 00]
 20    00     06 01 00
```

### 3.4 Watch sends "Next Track"

```
[EventId][Command]
 03       03
```

---

## 4. Relationship to the iOS (AMS) path

| Concept                  | iOS (AMS)                        | Android (CAMS)                      |
|--------------------------|----------------------------------|-------------------------------------|
| Now Playing in           | AMS Entity Update notifications  | CCS command `0x20` writes           |
| One attribute / update   | EntityID + AttributeID + value   | Media Attribute ID + length + value |
| Remote command out       | AMS Remote Command write         | CCS event `0x03` notification       |
| Command value space      | `Ble::AMS::RemoteCommandID`      | same numeric values (section 2.2)   |
| In-firmware consumer     | `MediaService` (via `ClientAMS`) | `MediaService` (via CCS)            |

The same parallel holds for notifications: ANCS (iOS) / CANS (Android), consumed by
`NotificationService`.

