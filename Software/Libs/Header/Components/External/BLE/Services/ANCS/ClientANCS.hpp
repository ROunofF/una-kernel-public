/**
 ******************************************************************************
 * @file    ClientANCS.hpp
 * @brief   The Apple Notification Center Service (ANCS).
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef BLE_ANCS_CLIENT_ANCS_HPP
#define BLE_ANCS_CLIENT_ANCS_HPP

#include <array>

#include "IBle.hpp"
#include "OS.hpp"
#include "IGattClient.hpp"
#include "PeripheralLinkClients.hpp"

namespace Ble
{

/**
 * @brief   The Apple Notification Center Service (ANCS) client.
 *
 * Declares the ANCS service and characteristics to the discovery coordinator,
 * subscribes to the Notification Source (and Data Source, when present) once the
 * handles are resolved, and forwards notifications to the attached callback.
 */
class ClientANCS : public IGattClient {
public:

    /**
     * @brief   Receives ANCS source notifications.
     */
    class Callback {
    public:

        /**
         * @brief   Notification Source: a new/modified/removed iOS notification.
         * @param   pData: raw notification-source bytes.
         * @param   len: length in bytes.
         */
        virtual void onNotifSource(const uint8_t* pData, uint16_t len) {}

        /**
         * @brief   Data Source: a response to a Control Point request.
         * @param   pData: raw data-source bytes.
         * @param   len: length in bytes.
         */
        virtual void onDataSource(const uint8_t* pData, uint16_t len) {}

        /**
         * @brief   The link is set up; Control Point requests are now usable.
         * @note    Until this fires, a request would collide with discovery; the
         *          consumer should hold non-essential requests rather than retry.
         */
        virtual void onReady() {}

    protected:
        virtual ~Callback() = default;
    };

    /**
     * @brief   Get the singleton instance.
     */
    static ClientANCS& GetInstance()
    {
        static ClientANCS sInstance;
        return sInstance;
    }

    /**
     * @brief   Initialise UUIDs and register with the discovery coordinator.
     */
    void init();

    /**
     * @brief   Attach the callback that receives ANCS source notifications.
     */
    void attachANCSCallback(Callback* pCallback);

    /**
     * @brief   Write raw data to the Control Point characteristic.
     */
    bool writeControlPoint(const uint8_t* pData, uint16_t len);

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
    void onLinkReady() override;

private:

    // Setup step after the service is resolved: enable the two CCCDs one at a
    // time, then report readiness to the coordinator.
    enum class Setup {
        IDLE = 0,
        SUBSCRIBE_DATA,
        SUBSCRIBE_NOTIFICATION,
        READY
    };

    enum CharID {
        CHR_NOTIFICATION_SOURCE = 0,
        CHR_CONTROL_POINT,
        CHR_DATA_SOURCE,

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
    uint8_t mRetry { 0 };
    OS::Timer<ClientANCS> mTimer;

    void timCallback(void* arg);
    // Runs on the BLE event thread: performs the current setup step. Marshalled
    // there from timCallback so it does not race the event thread or run its
    // blocking GATT writes on the shared timer-daemon task.
    static void workTrampoline(void* ctx);
    void runSetupStep();
    void subscribeData();
    void subscribeNotification();
    void copyHandles(const ResolvedService& res);
};

} // namespace Ble

#endif // BLE_ANCS_CLIENT_ANCS_HPP
