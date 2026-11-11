/**
 ******************************************************************************
 * @file    Types.hpp
 * @brief   Domain types of the Activity subsystem.
 *
 *          Pure data types: no I/O, no FreeRTOS, no FS dependencies.
 *          Safe to include from anywhere, including unit tests.
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_TYPES_HPP
#define COMPONENT_ACTIVITY_TYPES_HPP

#include <cstdint>

namespace Activity
{

/**
 * @brief Abstract identifier for sensors the activity FSM controls.
 *
 * The FSM never sees concrete @c Sensor::Connection objects - it speaks
 * only in terms of these tokens through @ref ISensorControl.
 */
enum class FsmSensor : uint8_t
{
    PEDO = 0,
    FLOOR,
    HR,
    MOTION,
    TOUCH,
    COUNT
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_TYPES_HPP
