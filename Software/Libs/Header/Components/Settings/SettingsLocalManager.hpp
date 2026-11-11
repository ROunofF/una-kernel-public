/**
 ******************************************************************************
 * @file    SettingsLocalManager.hpp
 * @brief   Local settings management.
 ******************************************************************************
 */

#ifndef __SETTINGS_LOCAL_MANAGER_HPP
#define __SETTINGS_LOCAL_MANAGER_HPP

#include "SettingsManagerBase.hpp"
#include "WatchSettingsLocal.hpp"

#include <vector>
#include <string>

namespace Settings
{

class LocalManager : public Settings::ManagerBase<WatchSettingsLocal> {
public:
    static LocalManager& GetInstance()
    {
        static LocalManager sInstance;
        return sInstance;
    }

protected:
    virtual bool parse(const char *buff, size_t length,
            WatchSettingsLocal &settings) override;

    virtual size_t create(const WatchSettingsLocal &settings, char *buff,
            size_t buffSize) override;

private:
    LocalManager();
};

} /* namespace Settings */

#endif /* __SETTINGS_LOCAL_MANAGER_HPP */
