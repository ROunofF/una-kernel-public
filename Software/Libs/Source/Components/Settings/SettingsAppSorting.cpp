/**
 ******************************************************************************
 * @file    WatchSettingsAppSorting.hpp
 * @brief   Manage local watch settings for sorting user applications
 ******************************************************************************
 *
 * @code{.json}
 * {
 *   "glances": [
 *     "Battery",
 *     "Steps"
 *   ],
 *   "utilities": [
 *     { "name": "Alarm",     "count": 3 },
 *     { "name": "Stopwatch", "count": 1 }
 *   ],
 *   "activities": [
 *     { "name": "Bike",  "count": 1 },
 *     { "name": "Hike",  "count": 2 },
 *     { "name": "HRMon", "count": 3 },
 *     { "name": "Run",   "count": 4 }
 *   ]
 * }
 * @endcode
 */

#include "SettingsAppSorting.hpp"

#define LOG_MODULE_PRX      "Settings.AppSort"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "main.h"
#include "SDK/JSON/JsonStreamWriter.hpp"

#include <algorithm>
#include <string_view>

namespace Settings
{

AppSorting::AppSorting() :
        Settings::ManagerBase<WatchSettingsAppSorting>(SETTINGS_APPSORTING_FILE_FULL_PATH)
{
}

bool AppSorting::parse(const char*              buff,
                       size_t                   length,
                       WatchSettingsAppSorting& settings)
{
    SDK::JsonStreamReader r {buff, length};
    if (!r.validate()) {
        LOG_ERROR("JSON is invalid\n");
        return false;
    }

    uint32_t oldVersion = 0;
    r.get("version", oldVersion);
    // Set current version
    settings.version = WatchSettingsAppSorting::kVersion;

    loadAppOrder(r, "activities", settings.activities);
    loadAppOrder(r, "utilities", settings.utilities);
    loadAppOrder(r, "glances", settings.glances);

    return true;
}

size_t AppSorting::create(const WatchSettingsAppSorting& settings,
                          char*                          buff,
                          size_t                         length)
{
    SDK::JsonStreamWriter w {buff, length};
    w.startMap();

    w.add("version", WatchSettingsAppSorting::kVersion);

    w.startArray("glances");
    for (uint8_t i = 0; i < settings.glances.size(); ++i) {
        w.add(settings.glances[i].c_str());
    }
    w.endArray();

    w.startArray("utilities", settings.utilities.size());
    for (uint8_t i = 0; i < settings.utilities.size(); ++i) {
        SDK::JsonStreamWriter::MapScope m{w};
        w.add("name", settings.utilities[i].name.c_str());
        w.add("count", settings.utilities[i].count);
    }
    w.endArray();

    w.startArray("activities", settings.activities.size());
    for (uint8_t i = 0; i < settings.activities.size(); ++i) {
        SDK::JsonStreamWriter::MapScope m{w};
        w.add("name", settings.activities[i].name.c_str());
        w.add("count", settings.activities[i].count);
    }
    w.endArray();

    w.endMap();

    if (w.isError()) {
        LOG_ERROR("Failed to create JSON\n");
        return 0;
    }

    return strlen(buff);
}

void AppSorting::loadAppOrder(SDK::JsonStreamReader&      r,
                                const char*               queryName,
                                std::vector<std::string>& names)
{
    names.clear();

    size_t outLength = 0;
    if (r.getArrayLength(queryName, outLength)) {
        for (size_t idx = 0; idx < outLength; ++idx) {
            char query[32];
            std::string_view appName;
            snprintf(query, sizeof(query), "%s[%d]", queryName, (int)idx);
            if (!r.get(query, appName) || appName.empty()) {
                continue;
            }

            const bool exists = (std::find(names.begin(), names.end(), appName) != names.end());

            if (!exists) {
                LOG_DEBUG("app %.*s\n", (int)appName.size(), appName.data());
                names.emplace_back(appName);
            }
        }
    }
}

void AppSorting::loadAppOrder(SDK::JsonStreamReader&    r,
                              const char*              queryName,
                              std::vector<FreqUseApp>& apps)
{
    apps.clear();

    size_t outLength = 0;
    if (r.getArrayLength(queryName, outLength)) {
        for (size_t idx = 0; idx < outLength; ++idx) {
            char query[32];

            FreqUseApp app;

            std::string_view appName;
            snprintf(query, sizeof(query), "%s[%d].name", queryName, (int)idx);
            if (!r.get(query, appName) || appName.empty()) {
                continue;
            }

            app.name = appName;

            snprintf(query, sizeof(query), "%s[%d].count", queryName, (int)idx);
            if (!r.get(query, app.count)) {
                continue;
            }

            const bool exists = (std::find_if(apps.begin(), apps.end(),
                [&](const FreqUseApp& x) { return x.name == app.name; }) != apps.end());

            if (!exists) {
                LOG_DEBUG("app %-20s : %u\n", app.name.c_str(), app.count);
                apps.push_back(std::move(app));
            }
        }
    }
}

} /* namespace Settings */
