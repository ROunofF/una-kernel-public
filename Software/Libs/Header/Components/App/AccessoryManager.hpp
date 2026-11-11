/**
 ******************************************************************************
 * @file    AccessoryManager.hpp
 * @brief   Orchestrates external BLE accessory acquisition for apps.
 ******************************************************************************
 *
 * The Backend-side brain for the external-accessory feature (HR straps in v1).
 * Handles the app opt-in (REQUEST_ACCESSORY_PREPARE / _RELEASE), gates on the
 * Settings::Accessories enable flag, drives Ble::AccessoryCentral, and bridges
 * decoded samples into the sensor layer (Sensor::Body::submitExternalHr), where
 * the HR source arbiter decides whether they or the optical sensor feed apps.
 * AccessoryCentral stays pure BLE; all policy lives here.
 *
 * A singleton (reached from the app message handler, mirroring Sensor::Manager)
 * so it needs no construction-time wiring; it reaches AccessoryCentral and the
 * sensor Body through their own accessors on first use.
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_APP_ACCESSORY_MANAGER_HPP
#define __COMPONENT_APP_ACCESSORY_MANAGER_HPP

#include <cstdint>

#include "Accessory/AccessoryCentral.hpp"

namespace App
{

class AccessoryManager : public Ble::AccessoryCentral::SampleCallback {
public:

    static AccessoryManager& getInstance()
    {
        static AccessoryManager sInstance;
        return sInstance;
    }

    /**
     * @brief   Begin acquiring the requested accessory kinds for an activity.
     * @param   kinds: bitmask of SDK::Accessory::Kind (HRM in v1).
     * @param   ownerPid: PID of the requesting app service (for release on
     *          that app's termination).
     * @retval  'true' if acquisition started; 'false' if the feature is
     *          disabled, no matching kind, or BLE is unavailable.
     */
    bool prepare(uint32_t kinds, uint32_t ownerPid);

    /**
     * @brief   Stop acquisition: disconnect the accessory and stop scanning.
     * @param   callerPid: PID of the requesting app service. No-ops unless it
     *          matches the current owner, so one app can't release another's link.
     */
    void release(uint32_t callerPid);

    /**
     * @brief   Apply a live settings mode change (Settings ▸ Sensors). Off stops
     *          any active acquisition immediately; External & Wrist / External
     *          Only re-applies the wrist-fallback policy mid-session. No-op when
     *          nothing is acquiring (the next prepare() reads the new mode).
     * @param   mode: WatchSettingsAccessories::Mode value.
     */
    void applyModeChange(uint8_t mode);

    /**
     * @brief   Lifecycle safety net: an app process terminated. Releases if it
     *          was the owner, so a crashed app never leaves a strap connected.
     */
    void onAppStopped(uint32_t pid);

    /**
     * @brief   Whether acquisition is currently active.
     */
    bool isActive() const { return mActive; }

    /**
     * @brief   Forward an accessory link-status change to the owner app's
     *          service as EVENT_ACCESSORY_STATUS, so the app can show a
     *          pre-activity/in-workout indicator. Called by the Backend
     *          status sink. No-op if there is no owner or the app has gone.
     * @param   sdkState: SDK::Accessory::State value for the link.
     * @param   name: device name (may be empty/NULL).
     */
    void forwardStatusToOwner(uint8_t sdkState, const char *name);

    // Ble::AccessoryCentral::SampleCallback
    void onAccessorySample(const Ble::AccessorySample &sample) override;

private:
    AccessoryManager() = default;

    // Unconditional teardown (owner check already done / not required, e.g. a
    // settings-driven disable).
    void releaseActive();

    bool     mActive { false };
    bool     mCallbackAttached { false };
    uint32_t mOwnerPid { 0 };
};

} /* namespace App */

#endif /* __COMPONENT_APP_ACCESSORY_MANAGER_HPP */
