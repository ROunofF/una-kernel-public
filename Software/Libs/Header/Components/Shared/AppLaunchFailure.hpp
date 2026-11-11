/**
 ******************************************************************************
 * @file    AppLaunchFailure.hpp
 * @brief   Why an application failed to launch, for the user-facing message.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef APP_LAUNCH_FAILURE_HPP
#define APP_LAUNCH_FAILURE_HPP

#include <cstdint>

namespace App {

/**
 * @brief Why a launch failed, as far as the user needs to know.
 *
 *        Deliberately coarse. The log already records exactly what went wrong
 *        and is the right place for it; this exists only to choose which of a
 *        small number of sentences the watch shows, so splitting it finer
 *        would add plumbing without changing anything the user sees.
 *
 *        NOT a protocol value. Unlike SystemError, this stays inside the
 *        watch - nothing here reaches the phone - so it can be renumbered
 *        freely if that ever helps. Keep it that way: a failed launch is a
 *        transient, user-triggered event, not a condition the device is in.
 */
enum class LaunchFailure : uint8_t {
    /// No failure. The launch succeeded, or none has been attempted.
    NONE = 0,

    /// The image did not fit. The headline case: the heap has enough
    /// free bytes in total but no contiguous run large enough, so this can
    /// happen on a watch reporting plenty of free memory. It is also the one
    /// the user can act on, by closing something.
    OUT_OF_MEMORY,

    /// The .uapp could not be read or is not valid for this kernel - a CRC
    /// reject, a malformed header, or an ABI the kernel does not implement.
    /// Nothing the user can do; it is the app that is wrong.
    BAD_IMAGE,

    /// The app was asked to start and did not answer in time. Distinct from
    /// the above because the app may yet be fine - the watch was busy, or
    /// something upstream is wedged.
    TIMEOUT,

    /// The launcher asked for an app the manager does not have. Normally a
    /// stale icon for a .uapp that has since been deleted.
    NOT_FOUND,

    /// Failed for a reason not worth its own sentence.
    OTHER,
};

} // namespace App

#endif // APP_LAUNCH_FAILURE_HPP
