/**
 ******************************************************************************
 * @file    SettingsDebugManager.hpp
 * @brief   Debug settings management.
 ******************************************************************************
 */

#ifndef __SETTINGS_DEBUG_MANAGER_HPP
#define __SETTINGS_DEBUG_MANAGER_HPP

#include "SettingsManagerBase.hpp"
#include "WatchSettingsDebug.hpp"

#include <vector>
#include <string>

namespace Settings
{

class DebugManager : public Settings::ManagerBase<WatchSettingsDebug> {
public:
    static DebugManager& GetInstance()
    {
        static DebugManager sInstance;
        return sInstance;
    }

protected:
    virtual bool parse(const char *buff, size_t length,
            WatchSettingsDebug &settings) override;

    virtual size_t create(const WatchSettingsDebug &settings, char *buff,
            size_t buffSize) override;

private:
    DebugManager();
};

} /* namespace Settings */

#endif /* __SETTINGS_DEBUG_MANAGER_HPP */
