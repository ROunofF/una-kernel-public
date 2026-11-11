/**
 ******************************************************************************
 * @file    WatchSettingsAppSorting.hpp
 * @brief   Local watch settings for user apps sorting
 ******************************************************************************
 */

#ifndef __WATCH_SETTINGS_APP_SORTING_HPP
#define __WATCH_SETTINGS_APP_SORTING_HPP

#include <cstdint>
#include <vector>
#include <string>

struct FreqUseApp {
    std::string name;
    uint32_t    count;

    bool operator==(const FreqUseApp& other) const
    {
        return name == other.name && count == other.count;
    }

    bool operator!=(const FreqUseApp& other) const
    {
        return !(*this == other);
    }
};

/**
 * @brief Local device settings configuration structure.
 * @note Contains settings accessible only from the watch interface, not from mobile app.
 */
struct WatchSettingsAppSorting
{
    /// 2: utilities carry a use count, as activities do, so the top menu can
    ///    order them by frequency of use. A version 1 file lists them as bare
    ///    names; those are read as absent and rebuilt at count 0, which costs
    ///    nothing -- nothing ever wrote that list.
    static constexpr uint8_t kVersion = 2;
    uint32_t version = kVersion;

    // order removed -> direct lists
    std::vector<FreqUseApp>  activities;
    std::vector<FreqUseApp>  utilities;
    std::vector<std::string> glances;

    bool operator==(const WatchSettingsAppSorting& other) const
    {
        return version    == other.version       &&
               activities == other.activities &&
               utilities  == other.utilities  &&
               glances    == other.glances;
    }

    bool operator!=(const WatchSettingsAppSorting& other) const
    {
        return !(*this == other);
    }
};

#endif // __WATCH_SETTINGS_APP_SORTING_HPP
