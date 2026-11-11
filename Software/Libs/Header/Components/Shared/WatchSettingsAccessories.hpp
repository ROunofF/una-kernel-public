/**
 ******************************************************************************
 * @file    WatchSettingsAccessories.hpp
 * @brief   Watch-local settings for remembered external BLE accessories.
 ******************************************************************************
 *
 * Persisted list of remembered accessories (HR straps for v1) plus the
 * external-HR source mode. Watch-local, not phone-synced for v1. Shaped for
 * multiple accessory kinds; only "hrm" ships now.
 *
 * @code{.json}
 * { "version": 2,
 *   "mode": 0,
 *   "accessories": [
 *     { "kind": "hrm", "addrType": 1, "addr": "AA:BB:CC:DD:EE:FF",
 *       "name": "Garmin HRM-Pro", "encrypted": false }
 *   ] }
 * @endcode
 *
 * `mode` replaces the v1 boolean `enabled`; v1 files migrate on read
 * (enabled:true -> ExternalAndWrist, false -> Off).
 */

#ifndef __WATCH_SETTINGS_ACCESSORIES_HPP
#define __WATCH_SETTINGS_ACCESSORIES_HPP

#include <cstdint>
#include <string>
#include <vector>

/**
 * @brief   One remembered accessory.
 */
struct RememberedAccessory {
    std::string kind;       // accessory kind, e.g. "hrm"
    uint8_t     addrType;   // BLE peer address type
    std::string addr;       // "AA:BB:CC:DD:EE:FF"
    std::string name;       // human-readable, from advertising/discovery
    bool        encrypted;  // strap requires pairing/encryption

    bool operator==(const RememberedAccessory& o) const
    {
        return kind == o.kind && addrType == o.addrType && addr == o.addr &&
               name == o.name && encrypted == o.encrypted;
    }
    bool operator!=(const RememberedAccessory& o) const { return !(*this == o); }
};

/**
 * @brief   External-accessory settings (watch-local).
 */
struct WatchSettingsAccessories {
    static constexpr uint8_t kVersion = 2;
    static constexpr uint8_t kMaxAccessories = 8;

    // External-HR source policy. Default Off: the feature stays inert until the
    // user enables it (keeps main releasable through the rollout).
    enum class Mode : uint8_t {
        Off              = 0,  // external HR disabled — wrist optical only
        ExternalAndWrist = 1,  // prefer the strap, fall back to wrist optical
        ExternalOnly     = 2,  // strap only — HR is invalid when no strap data
    };

    uint32_t version = kVersion;
    Mode     mode    = Mode::Off;
    std::vector<RememberedAccessory> accessories;

    // True when the external-HR feature should acquire a strap at all.
    bool isEnabled() const { return mode != Mode::Off; }
    // True when wrist-optical fallback is disallowed (report invalid instead).
    bool isExternalOnly() const { return mode == Mode::ExternalOnly; }

    bool operator==(const WatchSettingsAccessories& o) const
    {
        return version == o.version && mode == o.mode &&
               accessories == o.accessories;
    }
    bool operator!=(const WatchSettingsAccessories& o) const
    {
        return !(*this == o);
    }
};

#endif // __WATCH_SETTINGS_ACCESSORIES_HPP
