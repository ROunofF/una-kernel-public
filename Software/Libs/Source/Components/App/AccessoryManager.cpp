/**
 ******************************************************************************
 * @file    AccessoryManager.cpp
 * @brief   Orchestrates external BLE accessory acquisition for apps.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "App.Accessory"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "App/AccessoryManager.hpp"

#include <cstring>
#include <cstdio>

#include "SDK/Messages/AccessoryMessages.hpp"
#include "SettingsAccessories.hpp"
#include "Instances/InstanceSensorLayer.hpp"
#include "App/Manager.hpp"
#include "App/Application.hpp"

namespace App
{

bool AccessoryManager::prepare(uint32_t kinds, uint32_t ownerPid)
{
    if ((kinds & SDK::Accessory::Kind::HRM) == 0) {
        return false;  // nothing we support requested
    }

    // Don't let a second app hijack a link another app already owns. (The owner
    // re-preparing is fine; a crashed owner is cleared first via onAppStopped.)
    if (mActive && mOwnerPid != 0 && mOwnerPid != ownerPid) {
        LOG_WARNING("prepare ignored: HRM owned by 0x%08lX (caller 0x%08lX)\n",
                (unsigned long)mOwnerPid, (unsigned long)ownerPid);
        return false;
    }

    // Reload so a freshly-edited (settings screen) enable/strap list is honored.
    Settings::Accessories::GetInstance().load();
    WatchSettingsAccessories cfg =
            Settings::Accessories::GetInstance().getSettings();
    if (!cfg.isEnabled()) {
        LOG_INFO("prepare ignored: feature disabled\n");
        return false;
    }
    if (cfg.accessories.empty()) {
        // Strict filtering: never connect to an unsaved strap, so with
        // nothing remembered there is nothing to acquire -- pair one first.
        LOG_INFO("prepare ignored: no remembered straps\n");
        return false;
    }

    Ble::AccessoryCentral *central = Ble::AccessoryCentral::instance();
    if (central == nullptr) {
        LOG_WARNING("prepare ignored: no BLE accessory link\n");
        return false;
    }

    // Strict filtering: acquisition connects only to remembered straps. Pass
    // their addresses as the allow-list. The stored "AA:BB:..:FF" string is
    // display order (addr[5]..addr[0]); reverse to BLE wire order (addr[0]=LSB)
    // so it matches advertising-report addresses.
    central->clearAllowedDevices();
    uint8_t allowed = 0;
    for (const RememberedAccessory &a : cfg.accessories) {
        unsigned b[6];
        if (std::sscanf(a.addr.c_str(), "%x:%x:%x:%x:%x:%x",
                &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) == 6) {
            uint8_t addr[6];
            for (int i = 0; i < 6; ++i) {
                addr[i] = static_cast<uint8_t>(b[5 - i] & 0xFF);
            }
            central->addAllowedDevice(addr);
            ++allowed;
        }
    }
    if (allowed == 0) {
        LOG_WARNING("prepare ignored: no parseable strap addresses\n");
        return false;
    }

    if (!mCallbackAttached) {
        central->attachSampleCallback(this);
        mCallbackAttached = true;
    }

    // Source policy: External Only disables wrist-optical fallback (HR reads
    // invalid when the strap has no fresh data); External & Wrist keeps it.
    Instance::SensorLayer::getInstance().getSensorBody()
            .setHrExternalOnly(cfg.isExternalOnly());

    mOwnerPid = ownerPid;
    mActive = true;
    LOG_INFO("prepare: acquiring HRM (owner 0x%08lX, mode %u)\n",
            (unsigned long)ownerPid, static_cast<unsigned>(cfg.mode));
    central->requestAcquire();
    return true;
}

void AccessoryManager::release(uint32_t callerPid)
{
    if (!mActive) {
        return;
    }
    // Only the owning app may release the link.
    if (callerPid != mOwnerPid) {
        LOG_WARNING("release ignored: caller 0x%08lX != owner 0x%08lX\n",
                (unsigned long)callerPid, (unsigned long)mOwnerPid);
        return;
    }
    releaseActive();
}

void AccessoryManager::releaseActive()
{
    LOG_INFO("release\n");
    if (Ble::AccessoryCentral *central = Ble::AccessoryCentral::instance()) {
        central->release();
    }
    Sensor::Body &body = Instance::SensorLayer::getInstance().getSensorBody();
    // Restore wrist-optical fallback for background (non-workout) HR, and forget
    // the last strap sample so the arbiter can't prefer stale strap HR for the
    // freshness window after release.
    body.setHrExternalOnly(false);
    body.clearExternalHr();
    mActive = false;
    mOwnerPid = 0;
}

void AccessoryManager::applyModeChange(uint8_t mode)
{
    if (!mActive) {
        return;  // nothing acquiring; the next prepare() picks up the new mode
    }
    if (mode == static_cast<uint8_t>(WatchSettingsAccessories::Mode::Off)) {
        // User turned external HR off mid-session: stop acquisition now rather
        // than letting the scan/link run until the owning app exits.
        LOG_INFO("external HR disabled mid-session; releasing\n");
        releaseActive();
    } else {
        // External & Wrist <-> External Only: re-apply the fallback policy live.
        Instance::SensorLayer::getInstance().getSensorBody().setHrExternalOnly(
                mode == static_cast<uint8_t>(
                        WatchSettingsAccessories::Mode::ExternalOnly));
    }
}

void AccessoryManager::onAppStopped(uint32_t pid)
{
    if (mActive && pid == mOwnerPid) {
        LOG_INFO("owner app stopped; releasing accessory\n");
        release(pid);
    }
}

void AccessoryManager::forwardStatusToOwner(uint8_t sdkState, const char *name)
{
    if (mOwnerPid == 0) {
        return;  // no app owns the link (e.g. settings-screen activity only)
    }
    App::Application *app = App::Manager::GetInstance().findByPID(mOwnerPid);
    if (app == nullptr) {
        return;  // owner app has gone
    }

    // Runs on the Backend thread (the BLE-thread status callback defers the
    // forward here through the event queue). The pool is Manager-owned and
    // outlives the app, so alloc/release are safe. The queue push goes through
    // the app's lifecycle-locked sendToService(), which returns false when the
    // app is not instantiated -- so a stale instance's service queue is never
    // touched, and the message we still own is released below either way.
    App::MessageManager &mm = app->getMessageManager();
    auto *msg = mm.allocateMessage<SDK::Message::Accessory::EventStatus>();
    if (msg == nullptr) {
        return;
    }
    msg->kind  = SDK::Accessory::Kind::HRM;  // v1: HRM only
    msg->state = sdkState;
    if (name != nullptr) {
        strncpy(msg->name, name, sizeof(msg->name) - 1);
    }
    app->sendToService(msg);
    mm.releaseMessage(msg);
}

void AccessoryManager::onAccessorySample(const Ble::AccessorySample &sample)
{
    // Route on kind. AccessorySample carries cycling payloads too, and
    // `value` is meaningless for those -- without this guard a power or cadence
    // sample would be submitted as a heart rate. Cycling kinds are not yet
    // wired to Sensor::Cycling, so they are deliberately dropped.
    if (sample.kind != Ble::AccessorySample::Kind::HeartRate) {
        return;
    }

    // Bridge the strap sample into the sensor layer; the HR source arbiter in
    // Sensor::Body arbitrates external vs optical and feeds BodySubSensorHeartRate.
    uint8_t bpm = sample.value > 255 ? 255
                                     : static_cast<uint8_t>(sample.value);
    Instance::SensorLayer::getInstance().getSensorBody()
            .submitExternalHr(bpm, sample.trust);
}

} /* namespace App */
