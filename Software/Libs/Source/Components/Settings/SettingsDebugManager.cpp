/**
 ******************************************************************************
 * @file    SettingsDebugManager.hpp
 * @brief   Debug settings management.
 ******************************************************************************
 *
 * @code{.json}
 * {
 * }
 * @endcode
 */

#include "SettingsDebugManager.hpp"

#define LOG_MODULE_PRX      "Settings.Debug"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "main.h"
#include "SDK/JSON/JsonStreamReader.hpp"
#include "SDK/JSON/JsonStreamWriter.hpp"

#include <algorithm>
#include <string_view>

namespace Settings
{

DebugManager::DebugManager() :
        Settings::ManagerBase<WatchSettingsDebug>(SETTINGS_DEBUG_FILE_FULL_PATH)
{
}

bool DebugManager::parse(const char*         buff,
                         size_t              length,
                         WatchSettingsDebug& settings)
{
    SDK::JsonStreamReader r {buff, length};
    if (!r.validate()) {
        LOG_ERROR("JSON is invalid\n");
        return false;
    }

    uint32_t oldVersion = 0;
    r.get("version", oldVersion);
    // Set current version
    settings.version = WatchSettingsDebug::kVersion;

    r.get("ppgCfg.manufLog", settings.ppgCfg.manufLog);

    r.get("gpsCfg.nmeaLog",  settings.gpsCfg.nmeaLog);
    r.get("gpsCfg.imuLog",   settings.gpsCfg.imuLog);
    r.get("gpsCfg.binLog",   settings.gpsCfg.binLog);
    r.get("gpsCfg.MinSNR",   settings.gpsCfg.MinSNR);
    r.get("gpsCfg.hdopTh",   settings.gpsCfg.hdopTh);
    r.get("gpsCfg.haacLim",  settings.gpsCfg.haacLim);
    r.get("gpsCfg.elevMask", settings.gpsCfg.elevMask);

    r.get("wristCfg.enable",           settings.wristCfg.enable);
    r.get("wristCfg.minAngleFocus",    settings.wristCfg.minAngleFocus);
    r.get("wristCfg.minAngleNonfocus", settings.wristCfg.minAngleNonfocus);
    r.get("wristCfg.maxTiltLr",        settings.wristCfg.maxTiltLr);
    r.get("wristCfg.maxTiltLl",        settings.wristCfg.maxTiltLl);
    r.get("wristCfg.maxTiltPd",        settings.wristCfg.maxTiltPd);
    r.get("wristCfg.maxTiltPu",        settings.wristCfg.maxTiltPu);

    r.get("battCfg.logEnable", settings.battCfg.logEnable);
    r.get("battCfg.period",    settings.battCfg.period);

    r.get("bleCfg.useNUS", settings.bleCfg.useNUS);

    r.get("logCfg.logToBLE", settings.logCfg.logToBLE);
    r.get("logCfg.logToFile", settings.logCfg.logToFile);
    r.get("logCfg.logInDam", settings.logCfg.logInDam);

    r.get("fusionCfg.logToCSV", settings.fusionCfg.logToCSV);
    r.get("fusionCfg.logToBLE", settings.fusionCfg.logToBLE);

    return true;
}

size_t DebugManager::create(const WatchSettingsDebug& settings,
                            char*                     buff,
                            size_t                    buffSize)
{
    SDK::JsonStreamWriter w {buff, buffSize};
    w.startMap();

    w.add("version", WatchSettingsDebug::kVersion);

    {
        SDK::JsonStreamWriter::KeyedMapScope m{w, "ppgCfg"};
        w.add("manufLog", settings.ppgCfg.manufLog);
    }

    {
        SDK::JsonStreamWriter::KeyedMapScope m{w, "gpsCfg"};
        w.add("nmeaLog", settings.gpsCfg.nmeaLog);
        w.add("imuLog", settings.gpsCfg.imuLog);
        w.add("binLog", settings.gpsCfg.binLog);
        w.add("MinSNR", settings.gpsCfg.MinSNR);
        w.add("hdopTh", settings.gpsCfg.hdopTh);
        w.add("haacLim", settings.gpsCfg.haacLim);
        w.add("elevMask", settings.gpsCfg.elevMask);
    }

    {
        SDK::JsonStreamWriter::KeyedMapScope m{w, "wristCfg"};
        w.add("enable",           settings.wristCfg.enable);
        w.add("minAngleFocus",    settings.wristCfg.minAngleFocus);
        w.add("minAngleNonfocus", settings.wristCfg.minAngleNonfocus);
        w.add("maxTiltLr",        settings.wristCfg.maxTiltLr);
        w.add("maxTiltLl",        settings.wristCfg.maxTiltLl);
        w.add("maxTiltPd",        settings.wristCfg.maxTiltPd);
        w.add("maxTiltPu",        settings.wristCfg.maxTiltPu);
    }

    {
        SDK::JsonStreamWriter::KeyedMapScope m{w, "battCfg"};

        w.add("logEnable", settings.battCfg.logEnable);
        w.add("period",    settings.battCfg.period);
    }

    {
        SDK::JsonStreamWriter::KeyedMapScope m{w, "bleCfg"};

        w.add("useNUS", settings.bleCfg.useNUS);
    }

    {
        SDK::JsonStreamWriter::KeyedMapScope m{w, "logCfg"};

        w.add("logToBLE", settings.logCfg.logToBLE);
        w.add("logToFile", settings.logCfg.logToFile);
        w.add("logInDam", settings.logCfg.logInDam);
    }

    {
        SDK::JsonStreamWriter::KeyedMapScope m{w, "fusionCfg"};

        w.add("logToCSV", settings.fusionCfg.logToCSV);
        w.add("logToBLE", settings.fusionCfg.logToBLE);
    }

    w.endMap();

    if (w.isError()) {
        LOG_ERROR("Failed to create JSON\n");
        return 0;
    }

    return strlen(buff);
}

} /* namespace Settings */
