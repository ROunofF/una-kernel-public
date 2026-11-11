/**
 ******************************************************************************
 * @file    MediaService.hpp
 * @brief   Media Service: Now Playing consumer and remote-command source.
 ******************************************************************************
 *
 * Platform-agnostic media consumer. iOS feeds it through the AMS phone-link
 * client; Android through the Custom Command Service media command. Both produce
 * a single NowPlaying snapshot delivered to the attached callback. Remote commands
 * (play/pause/next/...) from the consumer are forwarded to whichever transport is
 * connected. The snapshot is reset and re-emitted on disconnect so the consumer
 * can clear it.
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BLE_SERVICE_MEDIA_HPP
#define __COMPONENT_BLE_SERVICE_MEDIA_HPP

#include <memory>

#include "IBle.hpp"
#include "ClientAMS.hpp"
#include "CustomCommandService.hpp"
#include "NowPlaying.hpp"
#include "Observer.hpp"

namespace Ble
{

/**
 * @brief   Media Service (Now Playing).
 *
 * Platform-agnostic media consumer: iOS feeds it through the AMS phone-link
 * client, Android through the Custom Command Service media command. Both produce
 * one NowPlaying snapshot and accept one remote-command set.
 */
class MediaService : virtual public Interface::IBle::StatusCallback,
        public ClientAMS::Callback,
        public CustomCommandService::IMediaCommandSink {
public:

    /**
     * @brief   Receives Now Playing snapshots.
     */
    class Callback {
    public:

        /**
         * @brief   The media state changed (or was cleared on disconnect).
         * @param   nowPlaying: immutable snapshot of the current media state.
         */
        virtual void onNowPlaying(const std::shared_ptr<NowPlaying>& nowPlaying) {}

    protected:
        virtual ~Callback() = default;
    };

    /**
     * @brief   Constructor.
     * @param   ble: BLE instance (used for the disconnect notification).
     * @param   ccs: Custom Command Service (the Android media transport).
     */
    MediaService(Interface::IBle& ble, CustomCommandService& ccs);

    /**
     * @brief   Destructor.
     */
    virtual ~MediaService();

    void attachCallback(Callback* pCallback);

    void detachCallback(Callback* pCallback);

    /**
     * @brief   Send a remote command to the phone player.
     * @param   command: a MediaCommand value (it mirrors AMS RemoteCommandID and
     *          CAMS MediaCommandID, so it maps onto either transport).
     * @retval  'true' if the command was accepted for sending.
     */
    bool sendRemoteCommand(uint8_t command);

private:

    Interface::IBle& mBle;
    CustomCommandService& mCcs;                   ///< Android media transport.
    NowPlaying mNowPlaying;                       ///< Working copy, mutated in place.
    uint32_t mElapsedSeq { 0 };                   ///< Monotonic counter, bumped per fresh elapsed.
    uint32_t mPlaybackSeq { 0 };                  ///< Monotonic counter, bumped per playback-state report.
    Observer::Holder<MediaService::Callback, 5> mObserver;

    // Publish the working copy as a fresh immutable snapshot to the consumer.
    void notifyNowPlaying();

    /**
     * @copydoc Ble::ClientAMS::Callback::onRemoteCommandSupported()
     */
    virtual void onRemoteCommandSupported(const uint8_t* pData,
            uint16_t len) override;

    /**
     * @copydoc Ble::ClientAMS::Callback::onEntityUpdate()
     */
    virtual void onEntityUpdate(const uint8_t* pData, uint16_t len) override;

    /**
     * @copydoc Ble::CustomCommandService::IMediaCommandSink::onMediaNowPlaying()
     */
    virtual bool onMediaNowPlaying(const uint8_t* pData, uint16_t len) override;

    /**
     * @copydoc Interface::IBle::StatusCallback::onDisconnect()
     */
    virtual void onDisconnect(uint8_t reason) override;
};

} // namespace Ble

#endif // __COMPONENT_BLE_SERVICE_MEDIA_HPP
