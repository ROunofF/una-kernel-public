/**
 ******************************************************************************
 * @file    ClientAMS.cpp
 * @brief   The Apple Media Service (AMS) client.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "BLE.ClientAMS"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"

#include "ClientAMS.hpp"

namespace Ble
{

///< CCCD value that enables notifications.
static constexpr uint16_t kCccdNotify = 0x0001;
///< Delay between consecutive successful setup steps.
static constexpr uint32_t kSetupStepMs = 100;
///< Delay before retrying a write the controller rejected.
static constexpr uint32_t kRetryMs = 400;
///< Retries for a subscribe step before it is abandoned (essential -> AMS
///< unavailable; optional -> skipped, setup continues).
static constexpr uint8_t kMaxSubRetry = 5;
///< Upper bound on the exponential backoff between registration attempts.
static constexpr uint32_t kRegMaxBackoffMs = 4000;
///< Quiet window the link must show before registration is attempted.
static constexpr uint32_t kQuietMs = 1500;
///< Poll interval while waiting for the link to become quiet.
static constexpr uint32_t kQuietPollMs = 500;
///< Bound on how long to wait for quiet before attempting anyway.
static constexpr uint8_t kMaxQuietWaits = 60;

// Media attributes registered for notifications, per entity.
static const uint8_t kTrackAttrs[] = {
    AMS::TrackAttributeIDArtist,
    AMS::TrackAttributeIDAlbum,
    AMS::TrackAttributeIDTitle,
    AMS::TrackAttributeIDDuration,
};

static const uint8_t kPlayerAttrs[] = {
    AMS::PlayerAttributeIDName,
    AMS::PlayerAttributeIDPlaybackInfo,
    AMS::PlayerAttributeIDVolume,
};

static const uint8_t kQueueAttrs[] = {
    AMS::QueueAttributeIDIndex,
    AMS::QueueAttributeIDCount,
    AMS::QueueAttributeIDShuffleMode,
    AMS::QueueAttributeIDRepeatMode,
};

// The media entities to register, in priority order (Track first: title /
// artist / album matter most). A bit per entity in mPending tracks what is left.
struct EntityReg {
    uint8_t entityID;
    const uint8_t* attrs;
    uint8_t attrCount;
    const char* name;
};

static const EntityReg kEntities[] = {
    { AMS::EntityIDTrack,  kTrackAttrs,  sizeof(kTrackAttrs),  "Track" },
    { AMS::EntityIDPlayer, kPlayerAttrs, sizeof(kPlayerAttrs), "Player" },
    { AMS::EntityIDQueue,  kQueueAttrs,  sizeof(kQueueAttrs),  "Queue" },
};
static constexpr uint8_t kEntityCount = sizeof(kEntities) / sizeof(kEntities[0]);

void ClientAMS::init()
{
    mSrvUUID.fromString("89d3502b-0f36-433a-8ef4-c502ad55f8dc");
    mChars[CHR_REMOTE_COMMAND].uuid.fromString("9b3c81d8-57b1-4a8a-b8df-0e56f7ca51c2");
    mChars[CHR_ENTITY_UPDATE].uuid.fromString("2f7cabce-808d-411f-9a0c-bb92ba96c102");
    mChars[CHR_ENTITY_ATTRIBUTE].uuid.fromString("c6b2f38c-23ab-46d8-a6ab-a3a870bbd5d7");

    mTimer.init(LOG_MODULE_PRX, this, &ClientAMS::timCallback, false, this);

    blePeripheralClients().registerClient(this);
}

void ClientAMS::attachAMSCallback(Callback* pCallback)
{
    mpCallback = pCallback;
}

void ClientAMS::copyHandles(const ResolvedService& res)
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

void ClientAMS::onServicesResolved(const ResolvedService& res)
{
    copyHandles(res);
    mRetry = 0;
    mQuietWaits = 0;
    mPending = 0;
    mRegCursor = 0;
    mRegBackoffMs = kRetryMs;
    mReleased = false;

    if (mChars[CHR_REMOTE_COMMAND].valueHandle == 0 ||
            mChars[CHR_ENTITY_UPDATE].valueHandle == 0) {
        LOG_WARNING("Mandatory AMS characteristic(s) not found\n");
        mSetup = Setup::IDLE;
        releaseManager();
        return;
    }

    // A CCCD that descriptor discovery did not confirm is the value+1 fallback,
    // which is wrong for AMS (its CCCD is at value+2). The subscribe write is
    // still attempted below, but a fallback CCCD can be locally accepted yet
    // silently ineffective (notifications never arrive), so make it visible.
    if (!mChars[CHR_ENTITY_UPDATE].cccdFromDescriptor) {
        LOG_WARNING("Entity Update CCCD unconfirmed, using fallback 0x%04X; "
                "Now Playing may not arrive\n",
                mChars[CHR_ENTITY_UPDATE].cccdHandle);
    }
    if (!mChars[CHR_REMOTE_COMMAND].cccdFromDescriptor) {
        LOG_WARNING("Remote Command CCCD unconfirmed, using fallback 0x%04X\n",
                mChars[CHR_REMOTE_COMMAND].cccdHandle);
    }

    mSetup = Setup::SUBSCRIBE_ENTITY_UPDATE;
    mTimer.start(kSetupStepMs);
}

bool ClientAMS::subscribe(uint16_t cccdHandle)
{
    uint16_t attrVal = kCccdNotify;
    return blePeripheralClients().ops().writeCharDescriptor(blePeripheralClients().connHandle(),
            cccdHandle, (uint8_t*) &attrVal, sizeof(attrVal));
}

bool ClientAMS::registerEntity(uint8_t entityID, const uint8_t* attrs,
        uint16_t attrCount)
{
    uint16_t len = AMS::BuildEntityUpdateRegistration(entityID, attrs,
            attrCount, mBuffer, sizeof(mBuffer));
    if (len == 0) {
        return false;
    }

    return blePeripheralClients().ops().writeCharValue(blePeripheralClients().connHandle(),
            mChars[CHR_ENTITY_UPDATE].valueHandle, mBuffer, len);
}

void ClientAMS::releaseManager()
{
    if (!mReleased) {
        mReleased = true;
        // Subscriptions are in place; let the coordinator proceed (and ungate
        // other clients) while entity registration completes in the background.
        blePeripheralClients().clientReady();
    }
}

void ClientAMS::gotoStep(Setup next)
{
    mRetry = 0;
    mQuietWaits = 0;
    mSetup = next;
    switch (next) {
        case Setup::REGISTER:
            mPending = (1u << kEntityCount) - 1;   // every entity still to register
            mRegCursor = 0;
            mRegBackoffMs = kRetryMs;
            mTimer.start(kSetupStepMs);
            break;
        case Setup::READY:
            LOG_INFO("Ready\n");
            break;
        default:
            mTimer.start(kSetupStepMs);
            break;
    }
}

void ClientAMS::subscribeStep(bool ok, Setup next, bool releaseOnSuccess,
        bool essential, const char* what)
{
    if (ok) {
        LOG_DEBUG("Subscribed %s\n", what);
        if (releaseOnSuccess) {
            releaseManager();
        }
        gotoStep(next);
        return;
    }

    if (++mRetry <= kMaxSubRetry) {
        LOG_DEBUG("Subscribe %s failed, retry %u/%u\n", what, mRetry,
                kMaxSubRetry);
        mTimer.start(kRetryMs);
        return;
    }

    if (essential) {
        // Without this characteristic there is no Now Playing at all: abort.
        LOG_WARNING("Subscribe %s gave up; AMS unavailable\n", what);
        releaseManager();
        mSetup = Setup::IDLE;
        return;
    }

    // Optional characteristic: carry on without it (the coordinator was already
    // released by the essential step that ran before this one).
    LOG_WARNING("Subscribe %s gave up; continuing without it\n", what);
    gotoStep(next);
}

void ClientAMS::registerTick()
{
    if (mPending == 0) {
        gotoStep(Setup::READY);
        return;
    }

    // Defer registration while the link is busy (a notification burst or file
    // sync), so writes are not issued to a controller that will reject them.
    if (!blePeripheralClients().transferQuiet(kQuietMs)) {
        if (++mQuietWaits == 1) {
            LOG_DEBUG("Register deferred: link busy\n");
        }
        if (mQuietWaits <= kMaxQuietWaits) {
            mTimer.start(kQuietPollMs);
            return;
        }
        // Waited long enough; attempt anyway.
    }
    mQuietWaits = 0;

    // Register the next pending entity; round-robin so a momentarily failing one
    // does not starve the others.
    while ((mPending & (1u << mRegCursor)) == 0) {
        mRegCursor = (mRegCursor + 1) % kEntityCount;
    }
    const EntityReg& e = kEntities[mRegCursor];

    if (registerEntity(e.entityID, e.attrs, e.attrCount)) {
        LOG_DEBUG("Registered %s\n", e.name);
        mPending &= ~(1u << mRegCursor);
        mRegBackoffMs = kRetryMs;     // a write went through; reset the backoff
        mRegCursor = (mRegCursor + 1) % kEntityCount;
        mTimer.start(kSetupStepMs);   // next entity (or READY) promptly
        return;
    }

    // A sync 'false' here is the controller being momentarily busy (a genuine ATT
    // rejection would arrive later as a separate error event). Move on, back off,
    // and keep trying - registration is not abandoned while the link is up.
    LOG_DEBUG("Register %s busy, backoff %ums\n", e.name, mRegBackoffMs);
    mRegCursor = (mRegCursor + 1) % kEntityCount;
    mTimer.start(mRegBackoffMs);
    mRegBackoffMs *= 2;
    if (mRegBackoffMs > kRegMaxBackoffMs) {
        mRegBackoffMs = kRegMaxBackoffMs;
    }
}

void ClientAMS::timCallback(void* arg)
{
    (void) arg;
    // The timer fires on the RTOS timer-daemon task; the setup step mutates
    // client state and issues blocking GATT writes, so run it on the BLE event
    // thread. If the work queue is momentarily full, re-arm and retry.
    if (!blePeripheralClients().postToBleThread(&ClientAMS::workTrampoline,
            this)) {
        mTimer.start(kSetupStepMs);
    }
}

void ClientAMS::workTrampoline(void* ctx)
{
    static_cast<ClientAMS*>(ctx)->runSetupStep();
}

void ClientAMS::runSetupStep()
{
    switch (mSetup) {
        case Setup::SUBSCRIBE_ENTITY_UPDATE:
            // Essential: carries the Now Playing attributes. Release the
            // coordinator on success; abort AMS if it cannot be enabled.
            subscribeStep(subscribe(mChars[CHR_ENTITY_UPDATE].cccdHandle),
                    Setup::SUBSCRIBE_REMOTE_CMD, true, true, "Entity Update");
            break;
        case Setup::SUBSCRIBE_REMOTE_CMD:
            // Optional: only reports which remote commands the player supports.
            // A rejection must not stop the media entities being registered.
            subscribeStep(subscribe(mChars[CHR_REMOTE_COMMAND].cccdHandle),
                    Setup::REGISTER, false, false, "Remote Command");
            break;
        case Setup::REGISTER:
            registerTick();
            break;
        default:
            break;
    }
}

void ClientAMS::onServiceUnavailable()
{
    LOG_INFO("Not available\n");
    mSetup = Setup::IDLE;
}

void ClientAMS::onNotification(uint16_t attrHandle, const uint8_t* data,
        uint8_t len)
{
    if (attrHandle == mChars[CHR_REMOTE_COMMAND].valueHandle) {
        LOG_DEBUG("Remote Command list, len=%u\n", len);
        if (mpCallback != nullptr) {
            mpCallback->onRemoteCommandSupported(data, len);
        }
    } else if (attrHandle == mChars[CHR_ENTITY_UPDATE].valueHandle) {
        if (mpCallback != nullptr) {
            mpCallback->onEntityUpdate(data, len);
        }
    }
}

void ClientAMS::onLinkDown()
{
    // Stop the setup timer so no step fires against the torn-down link. A step
    // already queued on the BLE thread runs in Setup::IDLE and is a no-op.
    mTimer.stop();
    mSetup = Setup::IDLE;
    mRetry = 0;
    mPending = 0;
    mRegCursor = 0;
    for (CharInfo& info : mChars) {
        info.reset();
    }
}

bool ClientAMS::writeRemoteCommand(uint8_t cmd)
{
    if (mChars[CHR_REMOTE_COMMAND].valueHandle == 0) {
        return false;
    }
    if (!AMS::IsRemoteCommandID(cmd)) {
        return false;
    }
    // One GATT-client procedure at a time: do not collide with discovery.
    if (!blePeripheralClients().isReady()) {
        return false;
    }

    uint8_t packet;
    if (AMS::BuildRemoteCommand(static_cast<AMS::RemoteCommandID>(cmd), &packet,
            sizeof(packet)) == 0) {
        return false;
    }

    return blePeripheralClients().ops().writeCharValue(blePeripheralClients().connHandle(),
            mChars[CHR_REMOTE_COMMAND].valueHandle, &packet, sizeof(packet));
}

bool ClientAMS::writeEntityUpdate(const uint8_t* pData, uint16_t len)
{
    if (mChars[CHR_ENTITY_UPDATE].valueHandle == 0) {
        return false;
    }
    // One GATT-client procedure at a time: do not collide with discovery.
    if (!blePeripheralClients().isReady()) {
        return false;
    }

    return blePeripheralClients().ops().writeCharValue(blePeripheralClients().connHandle(),
            mChars[CHR_ENTITY_UPDATE].valueHandle, pData, len);
}

} // namespace Ble
