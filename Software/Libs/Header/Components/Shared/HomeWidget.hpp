/**
 ******************************************************************************
 * @file    HomeWidget.hpp
 * @brief   Home-screen widget state a running app pushes.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef HOME_WIDGET_HPP
#define HOME_WIDGET_HPP

#include <cstdint>

/**
 * @brief State of the single home-screen widget a running app pushes.
 *
 * The SDK WidgetShow bitmask is decoded into these flags at the kernel boundary
 * (where the SDK lives), so the GUI never depends on the SDK. percent is clamped
 * to 0..100; text is bounded and NUL-terminated (valid UTF-8 only when the
 * sender kept the contract). active=false clears the slot.
 */
struct HomeWidgetData {
    static constexpr uint8_t kTextBytes = 64;
    bool     active      = false;
    bool     showText    = false;   ///< draw the label
    bool     showPercent = false;   ///< draw the progress bar
    bool     fromEnd     = false;   ///< bar fill is pinned to the end (drains from the start)
    float    percent     = 0.0f;    ///< 0..100
    char     text[kTextBytes] {};   ///< label, UTF-8, NUL-terminated
};

#endif // HOME_WIDGET_HPP
