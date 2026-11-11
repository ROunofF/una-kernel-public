/**
 ******************************************************************************
 * @file    SettingsManager.cpp
 * @brief   Settings management.
 ******************************************************************************
 *
 * @code{.json}
 * {
 *   "version": 1,                              // Settings version
 *   "units": "metric",                         // units of the watch (e.g. 'metric' or 'imperial')
 *   "phone": {                                 // object, information about the phone
 *     "notifications": true,                   // boolean, whether to show notifications on the watch. Used only for Android.
 *   },
 *   "heartRateZones": [100, 150, 170, 190, 210, 230],    // array of heart rate zones
 *   "dailyGoals": {                            // Fields can be omitted or set to 0 if not specified.
 *     "activityMinutes": 120,
 *     "steps": 7000,
 *     "floors": 100
 *   },
 *   "height":178,
 *   "weight":73
 * }
 * @endcode
 */

#include "SettingsManager.hpp"

#define LOG_MODULE_PRX      "Settings"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include <string_view>
#include <algorithm>

#include "main.h"
#include "SDK/JSON/JsonStreamReader.hpp"
#include "SDK/JSON/JsonStreamWriter.hpp"

namespace Settings
{

static constexpr std::array<std::string_view, 2> kUnitsJsonValue = {"metric", "imperial"};

Manager::Manager() :
        Settings::ManagerBase<WatchSettings>(SETTINGS_FILE_FULL_PATH)
{
}

bool Manager::parse(const char *buff, size_t length,
        WatchSettings &settings)
{
    SDK::JsonStreamReader r {buff, length};
    if (!r.validate()) {
        LOG_ERROR("JSON is invalid\n");
        return false;
    }

    uint32_t oldVersion = 0;
    r.get("version", oldVersion);
    // Set current version
    settings.version = WatchSettings::kVersion;

    if (oldVersion < WatchSettings::kVersion) {
        LOG_WARNING("Old version detected! %u -> %u\n", oldVersion, WatchSettings::kVersion);
    }

    std::string_view units;

    r.get("units", units);
    auto it = std::find(kUnitsJsonValue.begin(), kUnitsJsonValue.end(), units);
    if (it != kUnitsJsonValue.end()) {
        settings.unitsImperial = (*it == kUnitsJsonValue[1]);
    }

    r.get("phone.notifications", settings.phone.notifications);

    for (uint8_t i = 0; i < HeartRateZones::kMaxThreshold; i++) {
        char query[32];
        snprintf(query, sizeof(query), "heartRateZones[%u]", i);
        r.get(query, settings.heartRateZones.thresholds[i]);
    }


    r.get("dailyGoals.activityMinutes", settings.dailyGoals.activityMinutes);
    r.get("dailyGoals.steps", settings.dailyGoals.steps);
    r.get("dailyGoals.floors", settings.dailyGoals.floors);

    r.get("height", settings.heightCm);
    r.get("weight", settings.weightKg);

    return true;
}

size_t Manager::create(const WatchSettings &settings,
        char *buff, size_t buffSize)
{
    SDK::JsonStreamWriter w {buff, buffSize};
    w.startMap();

    w.add("version", WatchSettings::kVersion);

    w.add("units", settings.unitsImperial ? kUnitsJsonValue[1] : kUnitsJsonValue[0]);

    {
        SDK::JsonStreamWriter::KeyedMapScope m{w, "phone"};
        w.add("notifications", settings.phone.notifications);
    }

    {
        SDK::JsonStreamWriter::KeyedArrayScope m{w, "heartRateZones"};
        for (uint8_t i = 0; i < HeartRateZones::kMaxThreshold; i++) {
            w.add(settings.heartRateZones.thresholds[i]);
        }
    }

    {
        SDK::JsonStreamWriter::KeyedMapScope m {w, "dailyGoals"};
        w.add("activityMinutes", settings.dailyGoals.activityMinutes);
        w.add("steps", settings.dailyGoals.steps);
        w.add("floors", settings.dailyGoals.floors);
    }

    w.add("height", settings.heightCm);
    w.add("weight", settings.weightKg);

    w.endMap();

    if (w.isError()) {
        LOG_ERROR("Failed to create JSON\n");
        return 0;
    }

    return strlen(buff);
}

} /* namespace Settings */
