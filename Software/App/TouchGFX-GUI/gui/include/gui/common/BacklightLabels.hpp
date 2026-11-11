/**
 ******************************************************************************
 * @file    BacklightLabels.hpp
 * @brief   Labels shared by the Backlight and Sleep Mode settings screens.
 ******************************************************************************
 */

#ifndef BACKLIGHTLABELS_HPP
#define BACKLIGHTLABELS_HPP

#include <texts/TextKeysAndLanguages.hpp>
#include <touchgfx/Unicode.hpp>
#include <touchgfx/TypedText.hpp>

#include <cstdint>

namespace Gui
{
namespace Labels
{

/// Texts of the backlight timeouts, in WatchSettingsLocal::Backlight::kTimeoutsS order.
constexpr TypedTextId kBacklightTimeouts[] = { T_TEXT_5_SEC, T_TEXT_10_SEC, T_TEXT_15_SEC };

/// Short day names, Monday first, in WatchSettingsLocal::Sleep::days order.
constexpr TypedTextId kDaysShort[] = {
    T_TEXT_MON, T_TEXT_TUE, T_TEXT_WED, T_TEXT_THU, T_TEXT_FRI, T_TEXT_SAT, T_TEXT_SUN
};

/// Full day names, Monday first.
constexpr TypedTextId kDaysLong[] = {
    T_TEXT_MONDAY, T_TEXT_TUESDAY, T_TEXT_WEDNESDAY, T_TEXT_THURSDAY,
    T_TEXT_FRIDAY, T_TEXT_SATURDAY, T_TEXT_SUNDAY
};

/**
 * @brief   The text for a timeout in seconds.
 * @note    A value no menu offers shows as the first one, which is also what
 *          the settings fall back to.
 */
inline TypedTextId backlightTimeout(uint8_t seconds)
{
    switch (seconds) {
        case 10: return T_TEXT_10_SEC;
        case 15: return T_TEXT_15_SEC;
        default: return T_TEXT_5_SEC;
    }
}

/**
 * @brief   Write a time of day, as minutes after midnight, in the clock format.
 * @param   twelveHour: "7:05a" rather than "07:05".
 * @retval  Characters written, not counting the terminator.
 */
inline uint16_t formatTime(touchgfx::Unicode::UnicodeChar *buf, uint16_t size,
                           uint16_t minutes, bool twelveHour)
{
    const int h = minutes / 60;
    const int m = minutes % 60;
    if (!twelveHour) {
        touchgfx::Unicode::snprintf(buf, size, "%02d:%02d", h, m);
        return touchgfx::Unicode::strlen(buf);
    }

    // %s here takes a UnicodeChar string, not a char one, so the suffix is
    // appended by hand.
    const int h12 = (h % 12 == 0) ? 12 : (h % 12);
    touchgfx::Unicode::snprintf(buf, size, "%d:%02d", h12, m);
    uint16_t n = touchgfx::Unicode::strlen(buf);
    if (n + 1 < size) {
        buf[n++] = (h < 12) ? 'a' : 'p';
        buf[n] = 0;
    }
    return n;
}

/**
 * @brief   Write a sleep window as "22:00-07:00" (or "10:00p-7:00a").
 */
inline void formatWindow(touchgfx::Unicode::UnicodeChar *buf, uint16_t size,
                         uint16_t startMin, uint16_t endMin, bool twelveHour)
{
    const uint16_t n = formatTime(buf, size, startMin, twelveHour);
    if (n + 1 >= size) {
        return;
    }
    buf[n] = '-';
    formatTime(buf + n + 1, static_cast<uint16_t>(size - n - 1), endMin, twelveHour);
}

/**
 * @brief   Copy a text, upper-casing ASCII letters. For a title from a label.
 */
inline void toUpper(touchgfx::Unicode::UnicodeChar *buf, uint16_t size, TypedTextId id)
{
    touchgfx::Unicode::strncpy(buf, touchgfx::TypedText(id).getText(), size);
    buf[size - 1] = 0;
    for (uint16_t i = 0; i < size && buf[i] != 0; ++i) {
        if (buf[i] >= 'a' && buf[i] <= 'z') {
            buf[i] = static_cast<touchgfx::Unicode::UnicodeChar>(buf[i] - 'a' + 'A');
        }
    }
}

} // namespace Labels
} // namespace Gui

#endif // BACKLIGHTLABELS_HPP
