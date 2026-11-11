/**
 ******************************************************************************
 * @file    SettingsManager.hpp
 * @brief   Settings management.
 ******************************************************************************
 */

#ifndef __SETTINGS_MANAGER_HPP
#define __SETTINGS_MANAGER_HPP

#include "SettingsManagerBase.hpp"
#include "WatchSettings.hpp"

namespace Settings
{

class Manager : public Settings::ManagerBase<WatchSettings> {
public:
    static Manager& GetInstance()
    {
        static Manager sInstance;
        return sInstance;
    }

protected:
    virtual bool parse(const char *buff, size_t length,
            WatchSettings &settings) override;

    virtual size_t create(const WatchSettings &settings, char *buff,
            size_t buffSize) override;

private:
    Manager();

};

} /* namespace Settings */

#endif /* __SETTINGS_MANAGER_HPP */
