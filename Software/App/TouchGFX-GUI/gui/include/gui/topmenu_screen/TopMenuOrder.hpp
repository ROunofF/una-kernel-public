/**
 ******************************************************************************
 * @file    TopMenuOrder.hpp
 * @brief   The order the top menu's entries appear in.
 ******************************************************************************
 */

#ifndef TOPMENUORDER_HPP
#define TOPMENUORDER_HPP

#include <gui/common/GuiConfig.hpp>

// The simulator does not carry Components/Shared on its include path, which is
// why every header here that needs App:: types reaches for it twice.
#if !defined(SIMULATOR)
#include "AppTypes.hpp"
#else // defined(SIMULATOR)
#include "../../../../Libs/Header/Components/Shared/AppTypes.hpp"
#endif // defined(SIMULATOR)

#include <cstdint>
#include <vector>

namespace Gui
{
namespace Config
{
namespace Menu
{
namespace Top
{

/**
 * @brief The three apps that hold fixed places in the top menu, by uapp id.
 *
 * By id and not by name: the id is what the app is built with and does not
 * change, whereas the name is shown to the user and one day gets translated.
 * An app that is not installed simply is not in the list, and the entries
 * after it move up.
 */
constexpr uint64_t kAppIdAlarm     = 0xA19C2A7E4F8B6D31ULL;
constexpr uint64_t kAppIdStopwatch = 0xA159B1C005CFD2A5ULL;
constexpr uint64_t kAppIdTimer     = 0xA1D74A3F0CFF98D3ULL;

/// Menu items an order can hold. Kept in step with MenuTop::kMaxItems by a
/// static_assert in TopMenuView.cpp -- this header cannot include MenuTop
/// without the container pulling the order back in.
constexpr int16_t kMaxOrderItems = 100;

/// Utility apps an order can hold: the fixed entries are always there.
/// Everything that builds an order has to pass the same cap or the screen and
/// the icon prefetch can lay out different menus.
constexpr int16_t kMaxOrderApps = kMaxOrderItems - ID_COUNT;

/**
 * @brief What one menu item refers to.
 *
 * The fixed entries used to be the first three items, so an item id below
 * ID_COUNT was a fixed entry and anything above it was the app at
 * id - ID_COUNT. They interleave now, so an id says nothing on its own and
 * every id has to be looked up here.
 */
struct ItemRef
{
    int16_t fixed  = -1;   ///< A Top::Id, or -1 when this item is an app.
    int16_t appIdx = -1;   ///< Index into the utility app list, or -1.
};

/**
 * @brief   Find an app in the utility list by its uapp id.
 * @retval  Its index, or -1 when it is not installed.
 */
inline int16_t appIndexForId(const std::vector<App::Item> *apps, int16_t maxApps,
                             uint64_t appId)
{
    if (apps == nullptr) {
        return -1;
    }

    const int16_t n = (static_cast<int16_t>(apps->size()) < maxApps)
                    ? static_cast<int16_t>(apps->size()) : maxApps;

    for (int16_t i = 0; i < n; ++i) {
        if ((*apps)[i].appId == appId) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief   Lay the top menu out.
 *
 * Alarm, Alerts, Power off, Settings, Sleep, Stopwatch, Timer, then every
 * other utility app in the order the backend gave them, which is most-used first.
 * Settings is fourth so that it lands on the arc's selected position when the
 * menu opens; if Alarm is not installed everything above Settings moves up and
 * the top of the arc is short by one.
 *
 * @param   apps: the utility app list, most-used first. May be null.
 * @param   maxApps: how many of it to consider.
 * @param   out: filled with one entry per menu item.
 * @param   max: capacity of @p out.
 * @retval  How many items the menu has.
 */
inline int16_t buildOrder(const std::vector<App::Item> *apps, int16_t maxApps,
                          ItemRef *out, int16_t max)
{
    if (out == nullptr || max <= 0) {
        return 0;
    }

    const int16_t alarm     = appIndexForId(apps, maxApps, kAppIdAlarm);
    const int16_t stopwatch = appIndexForId(apps, maxApps, kAppIdStopwatch);
    const int16_t timer     = appIndexForId(apps, maxApps, kAppIdTimer);

    int16_t count = 0;

    const auto pushApp = [&](int16_t idx) {
        if (idx >= 0 && count < max) {
            out[count].fixed  = -1;
            out[count].appIdx = idx;
            ++count;
        }
    };

    const auto pushFixed = [&](int16_t fixedId) {
        if (count < max) {
            out[count].fixed  = fixedId;
            out[count].appIdx = -1;
            ++count;
        }
    };

    pushApp(alarm);
    pushFixed(ID_ALERTS);
    pushFixed(ID_POWEROFF);
    pushFixed(ID_SETTINGS);
    pushFixed(ID_SLEEP);
    pushApp(stopwatch);
    pushApp(timer);

    if (apps != nullptr) {
        const int16_t n = (static_cast<int16_t>(apps->size()) < maxApps)
                        ? static_cast<int16_t>(apps->size()) : maxApps;

        for (int16_t i = 0; (i < n) && (count < max); ++i) {
            if (i == alarm || i == stopwatch || i == timer) {
                continue;   // already placed above
            }
            pushApp(i);
        }
    }

    return count;
}

/**
 * @brief   The item holding a given fixed entry, or -1.
 */
inline int16_t itemForFixed(const ItemRef *items, int16_t count, int16_t fixedId)
{
    for (int16_t i = 0; i < count; ++i) {
        if (items[i].fixed == fixedId) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief   The item showing a given app index, or -1.
 */
inline int16_t itemForAppIndex(const ItemRef *items, int16_t count, int16_t appIdx)
{
    if (appIdx < 0) {
        return -1;
    }

    for (int16_t i = 0; i < count; ++i) {
        if (items[i].appIdx == appIdx) {
            return i;
        }
    }

    return -1;
}

} // namespace Top
} // namespace Menu
} // namespace Config
} // namespace Gui

#endif // TOPMENUORDER_HPP
