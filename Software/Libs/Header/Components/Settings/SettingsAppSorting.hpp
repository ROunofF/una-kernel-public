/**
 ******************************************************************************
 * @file    SettingsAppSorting.hpp
 * @brief   Local settings management.
 ******************************************************************************
 */

#ifndef __SETTINGS_APP_SORTING_HPP
#define __SETTINGS_APP_SORTING_HPP

#include "SettingsManagerBase.hpp"
#include "WatchSettingsAppSorting.hpp"
#include "SDK/JSON/JsonStreamReader.hpp"

#include <vector>
#include <string>

namespace Settings
{

class AppSorting : public Settings::ManagerBase<WatchSettingsAppSorting> {
public:
    static AppSorting& GetInstance()
    {
        static AppSorting sInstance;
        return sInstance;
    }

protected:
    bool parse(const char*              buff,
               size_t                   length,
               WatchSettingsAppSorting& settings) override;

    size_t create(const WatchSettingsAppSorting& settings,
                  char*                          buff,
                  size_t                         length) override;

private:
    AppSorting();

    void loadAppOrder(SDK::JsonStreamReader&    r,
                      const char*               queryName,
                      std::vector<std::string>& names);

    void loadAppOrder(SDK::JsonStreamReader&   r,
                      const char*              queryName,
                      std::vector<FreqUseApp>& apps);
};

} /* namespace Settings */

#endif /* __SETTINGS_APP_SORTING_HPP */
