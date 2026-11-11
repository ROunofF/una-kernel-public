/**
 ******************************************************************************
 * @file    AppSorterFreqUse.hpp
 * @brief   Application list sorter by frequency of use
 ******************************************************************************
 */

#ifndef __APP_SORTER_FREQ_USE_HPP
#define __APP_SORTER_FREQ_USE_HPP

#include "WatchSettingsAppSorting.hpp"
#include "AppTypes.hpp"

#include <vector>
#include <cstdint>
#include <string_view>

namespace App
{

class SorterFreqUse
{
public:
    SorterFreqUse(std::vector<FreqUseApp>& order);

    virtual ~SorterFreqUse() = default;

    bool call(const char* appName);
    void sort(std::vector<Item>& apps);

private:
    static constexpr uint8_t kCountMax = 10;

    void synchronize(const std::vector<Item>& apps);
    bool hasApp(std::string_view name) const;

    std::vector<FreqUseApp>& mOrder;
};

} // namespace App

#endif // __APP_SORTER_FREQ_USE_HPP
