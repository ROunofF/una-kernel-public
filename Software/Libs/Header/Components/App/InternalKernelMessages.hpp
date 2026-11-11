/**
 * @file InternalKernelMessages.hpp
 * @brief Internal kernel message definitions
 * 
 * These messages are used for internal kernel component communication only.
 * Applications cannot send or receive these messages.
 * 
 * Internal message types use range: 0x0100_0000 - 0xFFFF_FFFF
 * 
 * KERNEL INTERNAL USE ONLY - DO NOT INCLUDE IN APPLICATION CODE
 */

#pragma once

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/MessageTypes.hpp"

// Force 4-byte alignment for all message structures
#pragma pack(push, 4)

namespace App
{

// =============================================================================
// Internal Kernel Message Types
// =============================================================================

namespace InternalMessageType {

    // Application lifecycle
    constexpr SDK::MessageType::Type APP_RUN_SRV            = 0x80010000;
    constexpr SDK::MessageType::Type APP_RUN_SRV_ANG_GUI    = 0x80020000;
    constexpr SDK::MessageType::Type APP_STOP_SRV_ANG_GUI   = 0x80030000;
    constexpr SDK::MessageType::Type APP_SUSPEND_GUI        = 0x80040000;
    constexpr SDK::MessageType::Type APP_RESUME_GUI         = 0x80050000;
    constexpr SDK::MessageType::Type APP_GLANCE_RUN         = 0x80060000;
    constexpr SDK::MessageType::Type APP_GLANCE_STOP        = 0x80070000;

    /// The GUI of an app whose service is already running, started by the
    /// kernel: a service's own request for it that did not fit, tried again
    /// once the kernel has made room.
    constexpr SDK::MessageType::Type APP_RUN_GUI            = 0x80080000;

}

struct InternalAppRunService : public SDK::MessageBase {
    InternalAppRunService() : SDK::MessageBase(InternalMessageType::APP_RUN_SRV) {}
};

struct InternalAppRun : public SDK::MessageBase {
    InternalAppRun() : SDK::MessageBase(InternalMessageType::APP_RUN_SRV_ANG_GUI) {}
};

struct InternalAppStop : public SDK::MessageBase {
    InternalAppStop() : SDK::MessageBase(InternalMessageType::APP_STOP_SRV_ANG_GUI) {}
};

struct InternalAppSuspend : public SDK::MessageBase {
    InternalAppSuspend() : SDK::MessageBase(InternalMessageType::APP_SUSPEND_GUI) {}
};

struct InternalAppResume : public SDK::MessageBase {
    InternalAppResume() : SDK::MessageBase(InternalMessageType::APP_RESUME_GUI) {}
};

struct InternalAppGlanceRun : public SDK::MessageBase {
    InternalAppGlanceRun() : SDK::MessageBase(InternalMessageType::APP_GLANCE_RUN) {}
};

struct InternalAppGlanceStop : public SDK::MessageBase {
    InternalAppGlanceStop() : SDK::MessageBase(InternalMessageType::APP_GLANCE_STOP) {}
};

struct InternalAppRunGui : public SDK::MessageBase {
    InternalAppRunGui() : SDK::MessageBase(InternalMessageType::APP_RUN_GUI) {}
};

} // namespace App

#pragma pack(pop)
