/**
 * @file AirohaCommandRegistry.hpp
 * @brief Central registry of all GPS commands
 */

#pragma once

#include "AirohaCommandContext.hpp"
#include <algorithm>

// Include commands
#include "AirohaCommandInternal.hpp"
#include "AirohaCommandGeneric.hpp"
#include "AirohaCommand.hpp"

namespace Airoha
{

/**
 * @brief Compile-time maximum size calculation using C++17 fold expressions
 */
template<typename... Types>
constexpr size_t max_sizeof() {
    return std::max({sizeof(Types)...});
}

/**
 * @brief Compile-time maximum alignment calculation
 */
template<typename... Types>
constexpr size_t max_alignof() {
    return std::max({alignof(Types)...});
}


// ========================================================================
// SINGLE PLACE TO REGISTER ALL COMMANDS
// Add your command type to this list
// ========================================================================
#define ALL_GPS_COMMANDS \
    Command::Internal, \
    Command::Generic, \
    Command::GenericSetBOOL, \
    Command::GenericGetBOOL, \
    Command::GenericSetUINT, \
    Command::GenericGetUINT, \
    Command::GenericSetINT, \
    Command::GenericGetINT, \
    Command::GenericSetFLOAT, \
    Command::GenericGetFLOAT, \
    Command::GetChipVersion, \
    Command::SetNavigationMode, \
    Command::GetNavigationMode, \
    Command::SetDebugLogOutput, \
    Command::GetDebugLogOutput, \
    Command::GetOdometerStatus, \
    Command::GetEpoStatus

    /* Add new commands here, separated by comma */



// Calculate maximum command size and alignment at compile time
static constexpr size_t MAX_COMMAND_SIZE = max_sizeof<ALL_GPS_COMMANDS>();
static constexpr size_t MAX_COMMAND_ALIGN = max_alignof<ALL_GPS_COMMANDS>();

} // namespace Airoha
