/**
 ******************************************************************************
 * @file    AppTypes.hpp
 * @brief   Common types of user applications used in the system
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __APP_TYPES_HPP
#define __APP_TYPES_HPP

#include "SDK/Glance/GlanceControl.h"

#include <cstdint>
#include <array>
#include <memory>

/**
 * @namespace App
 * @brief Namespace for application-specific types.
 */
namespace App
{

/**
 * @enum Type
 * @brief Enum representing the type of an application.
 */
enum class Type {
    kActivity = 0,
    kUtility,
    kGlance,
    kClockface,
    kUnknown
};


constexpr uint32_t kNameLen = 16;       ///< Length of the application name
constexpr uint32_t kIconWH = 60;        ///< Width and height of the application icon
constexpr uint32_t kIconSmallWH = 30;   ///< Width and height of the small application icon

/// How often an app on screen is offered a frame. A sampling rate rather than
/// a drawing rate: an app draws only when what it shows has changed, so a face
/// showing seconds draws once a second and one showing minutes once a minute,
/// whatever this is set to. Offering more often than the fastest thing on
/// screen changes is what keeps a change from being noticed late enough to
/// read as a skipped or repeated step.
constexpr uint32_t kFrameRate = 10;

constexpr uint32_t kGlanceRate = 1;      ///< Frame rate for the Glance updates

/**
 * @struct Capabilities
 * @brief Structure containing capabilities of an application.
 */
struct Capabilities {
    bool enPhoneNotification = true;    ///< Enable phone notification during the app
    bool enUsbChargingScreen = true;    ///< Enable USB charging screen during the app
    bool enMusicControl = true;         ///< Enable music control during the app

    bool operator==(const Capabilities &other) const
    {
        return (enPhoneNotification == other.enPhoneNotification &&
                enUsbChargingScreen == other.enUsbChargingScreen &&
                enMusicControl == other.enMusicControl);
    }

    bool operator!=(const Capabilities &other) const
    {
        return !(*this == other);
    }
};

constexpr uint32_t kGlanceControlsMax = 32;     ///< Maximum number of GlanceControls for a single application

// Only the icons have pointers to data,
// but since this data is only used for display,
// even if it is not valid it will not crash,
// it will just display garbage.
// All other data is a copy.
struct GlanceData {
    uint8_t count = 0;
    GlanceControl_t ctrls[App::kGlanceControlsMax];
    char altname[App::kNameLen] {};
};

/**
 * @struct Item
 * @brief Structure representing an item in the application list.
 *
 * @note Carries no icons -- see @ref IconSet. A list is as long as the apps
 *       installed, and is rebuilt every time a menu is opened, so what one
 *       entry costs is multiplied by both.
 */
struct Item {
    Type type = Type::kUnknown;     ///< Type of the application

    uint64_t appId = 0;             ///< Application ID
    uint32_t pidService = 0;        ///< Process ID of the service
    uint32_t pidGUI = 0;            ///< Process ID of the GUI

    char name[kNameLen]{};          ///< Name of the application

    Capabilities caps{};            ///< Capabilities of the application
};

/**
 * @struct IconSet
 * @brief One application's two icons, loaded on demand.
 *
 * Held apart from @ref Item because a pair of icons is ~4.5 KB while the rest
 * of an entry is a few dozen bytes: inline, a hundred-app list came to a
 * quarter of a megabyte in one allocation, every time a menu opened. A menu
 * instead names the handful of apps it is currently showing and is handed those
 * sets by shared_ptr, so a set it is drawing stays alive whatever the loader
 * does with the rest.
 */
struct IconSet {
    uint64_t appId = 0;             ///< Application these icons belong to

    /// Icon of the application
    std::array<uint8_t, kIconWH * kIconWH> icon {};

    /// Small icon of the application
    std::array<uint8_t, kIconSmallWH * kIconSmallWH> smallIcon {};
};

} // namespace App

#endif // __APP_TYPES_HPP
