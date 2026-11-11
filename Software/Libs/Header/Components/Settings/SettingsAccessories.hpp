/**
 ******************************************************************************
 * @file    SettingsAccessories.hpp
 * @brief   Persistent settings for remembered external BLE accessories.
 ******************************************************************************
 */

#ifndef __SETTINGS_ACCESSORIES_HPP
#define __SETTINGS_ACCESSORIES_HPP

#include "SettingsManagerBase.hpp"
#include "WatchSettingsAccessories.hpp"

namespace Settings
{

class Accessories : public Settings::ManagerBase<WatchSettingsAccessories> {
public:
    static Accessories& GetInstance()
    {
        static Accessories sInstance;
        return sInstance;
    }

protected:
    bool parse(const char*               buff,
               size_t                    length,
               WatchSettingsAccessories& settings) override;

    size_t create(const WatchSettingsAccessories& settings,
                  char*                           buff,
                  size_t                          length) override;

private:
    Accessories();
};

} /* namespace Settings */

#endif /* __SETTINGS_ACCESSORIES_HPP */
