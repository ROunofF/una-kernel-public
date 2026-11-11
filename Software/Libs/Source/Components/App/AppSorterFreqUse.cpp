/**
 ******************************************************************************
 * @file    AppSorterFreqUse.cpp
 * @brief   Application list sorter by frequency of use.
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sorter.FU"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "App/AppSorterFreqUse.hpp"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace App
{

/**
 * @brief Construct sorter bound to a persistent frequency/order list.
 *
 * The sorter operates on a shared list of application usage entries.
 * The list is expected to be persisted/loaded by the owner (e.g. from settings),
 * while this class updates it via @ref call() and uses it for sorting.
 *
 * @param[in,out] order Reference to external storage containing usage entries.
 *                      The reference must remain valid for the lifetime of this sorter.
 */
SorterFreqUse::SorterFreqUse(std::vector<FreqUseApp>& order)
    : mOrder(order)
{
}

/**
 * @brief Register a usage hit for an application and update usage weights.
 *
 * This strategy keeps all counters bounded and self-normalizing:
 * - The target application's counter is incremented (capped by @ref kCountMax).
 * - All other tracked applications are decremented by 1 down to 0.
 *
 * The result is a small bounded score that reflects recency/frequency:
 * frequently used apps stay near the cap, while unused apps decay toward zero.
 *
 * @param[in] appName Null-terminated application name.
 *                   If null or empty, the call is ignored.
 *
 * @note Only applications already present in @ref mOrder are tracked.
 *       If @p appName is not found in @ref mOrder, no changes are made.
 *
 * @post For all entries in @ref mOrder: 0 <= count <= @ref kCountMax.
 *
 * @complexity O(N), where N is the number of tracked applications in @ref mOrder.
 */
bool SorterFreqUse::call(const char* appName)
{
    if (!appName || *appName == '\0') {
        return false;
    }

#if 0
    LOG_INFO("order list --------------\n");
    for (const auto& o : mOrder) {
        LOG_INFO("%10s : %d\n", o.name.c_str(), o.count);
    }
#endif

    const std::string_view target{appName};

    auto it = std::find_if(mOrder.begin(), mOrder.end(),
        [&](const FreqUseApp& a) { return std::string_view{a.name} == target; });

    if (it == mOrder.end()) {
        return false; // Not tracked in mOrder.
    }

    auto& hit = *it;

    // Decrease others.
    for (auto& a : mOrder) {
        if (&a == &hit) {
            continue;
        }

        if (a.count > 0) {
            --a.count;
        }
    }

    bool changed = false;

    // Increase target (capped).
    if (hit.count < SorterFreqUse::kCountMax) {
        ++hit.count;
        changed = true;
    }

#if 0
    LOG_INFO("order list --------------\n");
    for (const auto& o : mOrder) {
        LOG_INFO("%10s : %d\n", o.name.c_str(), o.count);
    }
#endif

    return changed;
}

/**
 * @brief Bounded strlen for fixed-size buffers.
 *
 * @param[in]  s      Pointer to a character buffer.
 * @param[in]  maxLen Maximum number of characters to scan.
 * @return Number of characters until first '\\0' or @p maxLen if none found.
 */
static std::size_t cstrnlen(const char* s, std::size_t maxLen)
{
    std::size_t n = 0;
    while (n < maxLen && s[n] != '\0') {
        ++n;
    }
    return n;
}

/**
 * @brief Create a safe non-owning view of @ref Item::name.
 *
 * @ref Item::name is a fixed-size char array and may be not null-terminated.
 * This helper returns a @c std::string_view bounded by:
 *  - the first '\\0' within the buffer, or
 *  - the maximum buffer size (kNameLen).
 *
 * @param[in] it Application item.
 * @return String view pointing into @ref Item::name with a safe length.
 *
 * @warning The returned view is valid only as long as @p it is alive and unchanged.
 */
static std::string_view getAppName(const Item& it)
{
    const std::size_t n = cstrnlen(it.name, sizeof(it.name));
    return std::string_view{it.name, n};
}

/**
 * @brief Sort application items by usage frequency with an alphabetical fallback.
 *
 * The function first calls @ref synchronize() to align @ref mOrder with the
 * supplied @p apps list, and then sorts @p apps according to the rules below.
 *
 * Sorting rules:
 *  - Items present in @ref mOrder are placed first.
 *  - Items present in @ref mOrder are ordered by @ref FreqUseApp::count (descending).
 *  - If counts are equal, the relative order is resolved by the entry position in
 *    @ref mOrder (stable, deterministic tie-break).
 *  - Items not present in @ref mOrder are placed after tracked ones and sorted
 *    lexicographically by name.
 *
 * The algorithm uses @c std::stable_sort, so any elements considered equal by the
 * comparator keep their original relative order.
 *
 * @param[in,out] apps Vector of application items to be reordered in-place.
 */
void SorterFreqUse::sort(std::vector<Item>& apps)
{
    synchronize(apps);

    struct Info {
        uint32_t    count; ///< Usage counter
        std::size_t pos;   ///< Position in mOrder (tie-break)
    };

    // Build O(1) lookup table: app name -> {count, pos}
    std::unordered_map<std::string_view, Info> info;
    info.reserve(mOrder.size());

    for (std::size_t i = 0; i < mOrder.size(); ++i) {
        const auto& o = mOrder[i];
        info.emplace(std::string_view{o.name}, Info{o.count, i});
    }

    auto cmpFold = [](std::string_view a, std::string_view b) {
        const auto n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; ++i) {
            const unsigned char ca = static_cast<unsigned char>(a[i]);
            const unsigned char cb = static_cast<unsigned char>(b[i]);
            const unsigned char la = static_cast<unsigned char>(std::tolower(ca));
            const unsigned char lb = static_cast<unsigned char>(std::tolower(cb));
            if (la < lb) return true;
            if (la > lb) return false;
        }
        return a.size() < b.size();
    };

    std::stable_sort(apps.begin(), apps.end(),
        [&](const Item& a, const Item& b) {
            const auto na = getAppName(a);
            const auto nb = getAppName(b);

            const auto ia = info.find(na);
            const auto ib = info.find(nb);

            const bool aIn = (ia != info.end());
            const bool bIn = (ib != info.end());

            if (aIn && bIn) {
                if (ia->second.count != ib->second.count) {
                    return ia->second.count > ib->second.count; // Higher usage first
                }
                return ia->second.pos < ib->second.pos;         // Stable tie-break
            }

            if (aIn != bIn) {
                return aIn;                                     // Tracked entries first
            }

            return cmpFold(na, nb);                             // neither: alphabetical (case-insensitive)
        });
}

/**
 * @brief Synchronize @ref mOrder with the provided application list.
 *
 * This function keeps @ref mOrder consistent with the currently available apps:
 *  - Removes entries from @ref mOrder whose names are not present in @p apps.
 *  - Adds missing apps to @ref mOrder with @c count = 0.
 *
 * Names are matched by string value (safe for non-null-terminated @ref Item::name).
 *
 * @param[in] apps Vector of currently available applications.
 *
 * @note This function modifies @ref mOrder but does not reorder @p apps.
 */
void SorterFreqUse::synchronize(const std::vector<Item>& apps)
{
    // 1) Names currently present in apps
    std::unordered_set<std::string_view> appNames;
    appNames.reserve(apps.size());
    for (const auto& a : apps) {
        appNames.emplace(getAppName(a));
    }

    // 2) Remove from mOrder what is not present in apps
    mOrder.erase(
        std::remove_if(mOrder.begin(), mOrder.end(),
            [&](const FreqUseApp& o) {
                return appNames.find(std::string_view{o.name}) == appNames.end();
            }),
        mOrder.end());


    // 3) Add to mOrder what is not present in it
    for (const auto& app : apps) {
        const auto name = getAppName(app);
        if (!hasApp(name)) {
            mOrder.push_back(FreqUseApp{std::string(name), 0});
        }
    }

    // 4) Counters clamp
    for (auto& o : mOrder) {
        if (o.count > kCountMax) {
            o.count = kCountMax;
        }
    }
}

/**
 *  @brief Checks if the application name is present it mOrder list
 *
 *  @param[in] name Applicatio name
 *
 *  @return True if present, false otherwise
 */
bool SorterFreqUse::hasApp(std::string_view name) const
{
    if (name.empty()) {
        return false;
    }

    for (const auto& o : mOrder) {
        if (o.name == name) {
            return true;
        }
    }

    return false;
}

} // namespace App
