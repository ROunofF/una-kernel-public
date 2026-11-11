/**
 ******************************************************************************
 * @file    ErrorArbiter.hpp
 * @brief   Picks which system error the single error screen should show.
 *
 *          ErrorMonitor tracks many independent error conditions, but the GUI
 *          has one error screen. This arbitrates: the display is the highest
 *          priority active condition, subject to the user's acknowledgement.
 *
 *          Three concepts are kept separate. A *condition* is a flag owned by
 *          its producer (the thermal guard, USB protection, ...) and reflects a
 *          real hazard - the user never clears it. An *acknowledge* is the user
 *          hiding the visual; it does not clear the condition (charging stays
 *          inhibited by the producer). The *display* is what the screen shows.
 *
 *          Acknowledge raises a suppression level to the acknowledged error's
 *          priority: only a strictly higher condition re-shows (an escalation),
 *          so de-escalation does not re-nag. The level is reset by a full
 *          recovery (no conditions left) and by an explicit retry, so a user
 *          re-plugging the cable is told again why it still will not charge.
 *          TEMPERATURE_SHUTDOWN cannot be acknowledged - the watch is powering
 *          off.
 *
 *          Pure logic: no OS/GUI dependency, exercised directly by host tests.
 ******************************************************************************
 */

#ifndef ERROR_ARBITER_HPP
#define ERROR_ARBITER_HPP

#include <bitset>
#include <cstdint>

#include "SystemError.hpp"

class ErrorArbiter
{
public:
    /// Mark a condition active / cleared (called by the producers' set/clear).
    void setActive(SystemError e);
    void clearActive(SystemError e);

    /// User dismissed the current screen. Suppresses the current display (and
    /// anything at or below its priority) until something more urgent appears
    /// or everything recovers. No-op when nothing is shown or the top is the
    /// non-dismissable shutdown warning.
    void acknowledge();

    /// Drop the acknowledgement so every still-active condition can show again.
    /// Used when the user retries the action the errors block (re-plugging the
    /// cable): staying silent there reads as "the retry worked".
    void resetAcknowledge();

    /// The error the screen should show, or NONE for "leave the screen".
    SystemError currentDisplay() const;

    /// The most urgent condition that is actually active, whether or not the
    /// user acknowledged it, or NONE when everything has recovered. This is
    /// the state of the watch rather than the state of its screen, so it is
    /// what off-watch consumers (the phone) should be told about: hiding a
    /// message locally changes nothing they care about.
    SystemError currentCondition() const;

    bool   isActive(SystemError e) const { return mActive.test(idx(e)); }
    bool   anyActive() const             { return mActive.any(); }
    size_t activeCount() const           { return mActive.count(); }

    /// Priority of an error, higher = more urgent (0 = never shown).
    static uint8_t priorityOf(SystemError e);

    /// Whether the user may dismiss this error. The over-temperature warnings
    /// that require action (disconnect the cable / powering off) are not
    /// dismissable - they stay until their condition resolves.
    static bool isDismissable(SystemError e);

private:
    static constexpr size_t idx(SystemError e) { return static_cast<size_t>(e); }

    /// Highest priority active condition strictly above the given level.
    SystemError highestActiveAbove(uint8_t level) const;

    std::bitset<static_cast<size_t>(SystemError::COUNT)> mActive;
    uint8_t mAckLevel = 0;   ///< Suppress conditions with priority <= this.
};

#endif // ERROR_ARBITER_HPP
