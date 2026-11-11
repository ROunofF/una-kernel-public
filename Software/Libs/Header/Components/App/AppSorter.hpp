/**
 ******************************************************************************
 * @file    AppSorter.hpp
 * @brief   Application list sorter (custom order + alphabetical fallback).
 ******************************************************************************
 *
 * The sorter applies a user-defined ordering to a list of application names.
 * Items found in the configured order are placed first (in that order),
 * and all remaining items are appended sorted lexicographically.
 *
 ******************************************************************************
 */

#ifndef __APP_SORTER_HPP
#define __APP_SORTER_HPP

#include "AppTypes.hpp"

#include <vector>
#include <string>

namespace App
{

class Sorter
{
public:
    explicit Sorter(const std::vector<std::string>& order)
        : mOrder(order)
    {}

    virtual ~Sorter() = default;

    template<class T, class NameOf>
    void sort(std::vector<T>& apps, NameOf nameOf) const
    {
        constexpr auto kNotFound = std::numeric_limits<std::size_t>::max();

        auto orderPos = [&](std::string_view name) -> std::size_t {
            auto it = std::find(mOrder.begin(), mOrder.end(), name);
            if (it == mOrder.end())
                return kNotFound;
            return static_cast<std::size_t>(std::distance(mOrder.begin(), it));
        };

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
            [&](const T& a, const T& b) {
                const auto na = std::string_view{nameOf(a)};
                const auto nb = std::string_view{nameOf(b)};

                const auto pa = orderPos(na);
                const auto pb = orderPos(nb);

                const bool aIn = (pa != kNotFound);
                const bool bIn = (pb != kNotFound);

                if (aIn && bIn)  return pa < pb;  // both in mOrder
                if (aIn != bIn)  return aIn;      // only one in mOrder
                return cmpFold(na, nb);           // neither: alphabetical (case-insensitive)
            });
    }

private:
    const std::vector<std::string>& mOrder;
};

} // namespace App

#endif // __APP_SORTER_HPP
