/**
 ******************************************************************************
 * @file    ClientAMS.hpp
 * @brief   The Apple Media Service (AMS) client.
 ******************************************************************************
 *
 * Declares the AMS service and characteristics to the discovery coordinator,
 * subscribes to the Remote Command and Entity Update characteristics once the
 * handles are resolved, registers interest in the media entities, and forwards
 * notifications to the attached callback. Remote-control commands are written
 * to the Remote Command characteristic.
 *
 ******************************************************************************
 */

#ifndef BLE_AMS_CLIENT_AMS_HPP
#define BLE_AMS_CLIENT_AMS_HPP

#include <array>

#include "IBle.hpp"
#include "OS.hpp"
#include "IGattClient.hpp"
#include "PeripheralLinkClients.hpp"
#include "ParserAMS.hpp"

namespace Ble
{

/**
 * @brief   The Apple Media Service (AMS) client.
 */
class ClientAMS : public IGattClient {
public:

    /**
     * @brief   Receives AMS notifications.
     */
    class Callback {
    public:

        /**
         * @brief   Remote Command: the set of currently supported commands.
         * @param   pData: raw RemoteCommandID bytes.
         * @param   len: length in bytes.
         */
        virtual void onRemoteCommandSupported(const uint8_t* pData,
                uint16_t len) {}

        /**
         * @brief   Entity Update: a media attribute value (player/queue/track).
         * @param   pData: raw entity-update bytes.
         * @param   len: length in bytes.
         */
        virtual void onEntityUpdate(const uint8_t* pData, uint16_t len) {}

    protected:
        virtual ~Callback() = default;
    };

    /**
     * @brief   Get the singleton instance.
     */
    static ClientAMS& GetInstance()
    {
        static ClientAMS sInstance;
        return sInstance;
    }

    /**
     * @brief   Initialise UUIDs and register with the discovery coordinator.
     */
    void init();

    /**
     * @brief   Attach the callback that receives AMS notifications.
     */
    void attachAMSCallback(Callback* pCallback);

    /**
     * @brief   Send a remote-control command to the media player.
     * @param   cmd: a Ble::AMS::RemoteCommandID value.
     * @retval  'true' if the write was accepted.
     */
    bool writeRemoteCommand(uint8_t cmd);

    /**
     * @brief   Write raw data to the Entity Update characteristic.
     */
    bool writeEntityUpdate(const uint8_t* pData, uint16_t len);

    /**
     * @brief   True while an AMS media link is resolved on the connected phone.
     * @note    The Entity Update characteristic is the essential one, so its
     *          resolved handle marks an iOS device driving media over AMS. It is
     *          cleared on link-down. Callers use this to keep the Android (CAMS)
     *          path quiet while iOS is the active media transport.
     */
    bool isMediaLinkActive() const
    {
        return mChars[CHR_ENTITY_UPDATE].valueHandle != 0;
    }

    // --- IGattClient ------------------------------------------------------

    const Ble::UUID& serviceUuid() const override { return mSrvUUID; }
    uint8_t charCount() const override { return CHR_MAX; }
    const Ble::UUID& charUuid(uint8_t index) const override
            { return mChars[index].uuid; }
    void onServicesResolved(const ResolvedService& res) override;
    void onServiceUnavailable() override;
    void onNotification(uint16_t attrHandle, const uint8_t* data,
            uint8_t len) override;
    void onLinkDown() override;

private:

    // Setup steps after the service is resolved. Entity Update comes first: it
    // carries the Now Playing attributes and is essential. Remote Command (which
    // commands the player supports) is optional - its subscribe is best-effort so
    // a rejection does not stop the media entities being registered. Then the
    // entities are registered. One client procedure at a time.
    enum class Setup {
        IDLE = 0,
        SUBSCRIBE_ENTITY_UPDATE,
        SUBSCRIBE_REMOTE_CMD,
        REGISTER,
        READY
    };

    enum CharID {
        CHR_REMOTE_COMMAND = 0,
        CHR_ENTITY_UPDATE,
        CHR_ENTITY_ATTRIBUTE,

        CHR_MAX
    };

    struct CharInfo {
        Ble::UUID uuid;
        uint16_t valueHandle;
        uint16_t cccdHandle;
        bool cccdFromDescriptor;   ///< CCCD confirmed by discovery (not fallback).

        void reset()
        {
            valueHandle = 0;
            cccdHandle = 0;
            cccdFromDescriptor = false;
        }
    };

    Ble::UUID mSrvUUID;
    std::array<CharInfo, CHR_MAX> mChars;

    Callback* mpCallback { nullptr };

    Setup mSetup { Setup::IDLE };
    uint8_t mRetry { 0 };           ///< Retry counter for the current subscribe step.
    uint8_t mQuietWaits { 0 };      ///< Polls spent waiting for the link to go quiet.
    uint8_t mPending { 0 };         ///< Bitmask of entities still to be registered.
    uint8_t mRegCursor { 0 };       ///< Round-robin cursor over the pending entities.
    uint32_t mRegBackoffMs { 0 };   ///< Current backoff between registration attempts.
    bool mReleased { false };
    OS::Timer<ClientAMS> mTimer;
    uint8_t mBuffer[16] {};

    void timCallback(void* arg);
    // Runs on the BLE event thread: performs the current setup step. Marshalled
    // there from timCallback so it does not race the event thread or run its
    // blocking GATT writes on the shared timer-daemon task.
    static void workTrampoline(void* ctx);
    void runSetupStep();
    void copyHandles(const ResolvedService& res);
    bool subscribe(uint16_t cccdHandle);
    bool registerEntity(uint8_t entityID, const uint8_t* attrs,
            uint16_t attrCount);
    // A subscribe step: advance on success (optionally releasing the
    // coordinator), bounded-retry on failure. When exhausted, an essential step
    // aborts AMS (releases); an optional one carries on to the next step.
    void subscribeStep(bool ok, Setup next, bool releaseOnSuccess, bool essential,
            const char* what);
    // Registration tick: defer while the link is busy, otherwise register the
    // next pending entity (round-robin). Transient write failures back off and
    // are retried - registration is not abandoned while the link is up.
    void registerTick();
    void gotoStep(Setup next);
    void releaseManager();
};

} // namespace Ble

#endif // BLE_AMS_CLIENT_AMS_HPP
