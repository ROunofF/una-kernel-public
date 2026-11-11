/**
 ******************************************************************************
 * @file    SettingsAccessories.cpp
 * @brief   Persistent settings for remembered external BLE accessories.
 ******************************************************************************
 */

#include "SettingsAccessories.hpp"

#define LOG_MODULE_PRX      "Settings.Accessory"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "main.h"
#include "SDK/JSON/JsonStreamReader.hpp"
#include "SDK/JSON/JsonStreamWriter.hpp"

#include <cstring>
#include <string_view>

namespace Settings
{

Accessories::Accessories()
    : Settings::ManagerBase<WatchSettingsAccessories>(
            SETTINGS_ACCESSORIES_FILE_FULL_PATH)
{
}

bool Accessories::parse(const char*               buff,
                        size_t                    length,
                        WatchSettingsAccessories& settings)
{
    SDK::JsonStreamReader r {buff, length};
    if (!r.validate()) {
        LOG_ERROR("JSON is invalid\n");
        return false;
    }

    // Read the on-disk version so a file newer than we understand is at least
    // detected (best-effort parse, don't silently treat it as current).
    uint8_t fileVersion = 0;
    if (r.get("version", fileVersion) &&
        fileVersion > WatchSettingsAccessories::kVersion) {
        LOG_WARNING("accessories settings v%u newer than supported v%u; best-effort parse\n",
                fileVersion, WatchSettingsAccessories::kVersion);
    }
    settings.version = WatchSettingsAccessories::kVersion;

    // v2 tri-state "mode"; fall back to the v1 boolean "enabled" on migration
    // (enabled:true -> ExternalAndWrist, absent/false -> Off).
    settings.mode = WatchSettingsAccessories::Mode::Off;
    uint8_t mode = 0;
    if (r.get("mode", mode)) {
        if (mode <= static_cast<uint8_t>(WatchSettingsAccessories::Mode::ExternalOnly)) {
            settings.mode = static_cast<WatchSettingsAccessories::Mode>(mode);
        }
    } else {
        bool enabled = false;
        if (r.get("enabled", enabled) && enabled) {
            settings.mode = WatchSettingsAccessories::Mode::ExternalAndWrist;
        }
    }

    settings.accessories.clear();

    size_t count = 0;
    if (r.getArrayLength("accessories", count)) {
        for (size_t idx = 0;
             idx < count &&
             settings.accessories.size() < WatchSettingsAccessories::kMaxAccessories;
             ++idx)
        {
            char query[48];
            std::string_view sv;
            RememberedAccessory a {};

            snprintf(query, sizeof(query), "accessories[%d].kind", (int)idx);
            if (r.get(query, sv)) {
                a.kind.assign(sv.data(), sv.size());
            }

            snprintf(query, sizeof(query), "accessories[%d].addrType", (int)idx);
            r.get(query, a.addrType);

            snprintf(query, sizeof(query), "accessories[%d].addr", (int)idx);
            if (r.get(query, sv)) {
                a.addr.assign(sv.data(), sv.size());
            }

            snprintf(query, sizeof(query), "accessories[%d].name", (int)idx);
            if (r.get(query, sv)) {
                a.name.assign(sv.data(), sv.size());
            }

            snprintf(query, sizeof(query), "accessories[%d].encrypted", (int)idx);
            a.encrypted = false;
            r.get(query, a.encrypted);

            // An entry without an address is unusable — skip it.
            if (!a.addr.empty()) {
                settings.accessories.push_back(std::move(a));
            }
        }
    }

    return true;
}

size_t Accessories::create(const WatchSettingsAccessories& settings,
                           char*                           buff,
                           size_t                          length)
{
    SDK::JsonStreamWriter w {buff, length};
    w.startMap();

    w.add("version", WatchSettingsAccessories::kVersion);
    w.add("mode", static_cast<uint8_t>(settings.mode));

    w.startArray("accessories", settings.accessories.size());
    for (const RememberedAccessory& a : settings.accessories) {
        SDK::JsonStreamWriter::MapScope m{w};
        w.add("kind", a.kind.c_str());
        w.add("addrType", a.addrType);
        w.add("addr", a.addr.c_str());
        w.add("name", a.name.c_str());
        w.add("encrypted", a.encrypted);
    }
    w.endArray();

    w.endMap();

    if (w.isError()) {
        LOG_ERROR("Failed to create JSON\n");
        return 0;
    }

    return strlen(buff);
}

} /* namespace Settings */
