/**
 ******************************************************************************
 * @file    SystemError.hpp
 * @brief   System errors enumeration.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __SYSTEM_ERROR_HPP
#define __SYSTEM_ERROR_HPP

#include <cstdint>
#include <array>

/**
 * @brief System errors enumeration.
 *
 *        These values leave the watch: the phone receives them as the payload
 *        of the system-error event on the Custom Command Service. They are a
 *        protocol contract, not an internal detail - a phone build carries its
 *        own copy of this numbering. Append new codes at the end, and never
 *        renumber or reuse an existing one, or a phone will report the wrong
 *        error. Hence the explicit values.
 */
enum class SystemError : uint32_t {
    // No error. Doubles as the "nothing to show" answer from the arbiter and
    // as the value that tells the phone every condition has recovered, so it
    // is never itself an error being reported.
    NONE                 = 0,

    // The cable was seen connecting four times within ten seconds - an
    // intermittent contact in the cable or the connector, not a user plugging
    // it in. Charging keeps working; this only warns.
    CABLE_CONNECTION     = 1,

    // VSYS on the schematic - the PMIC system supply - out of range. Nothing
    // raises this yet: the source would be the PCA9420 asysPrewarning
    // interrupt, which is deliberately left masked until the flag clearing
    // around it is corrected (a TODO sits beside the interrupt mask in the PMIC
    // driver). The GUI renders it and the protocol publishes it, so the value
    // stays reserved rather than being reused.
    SYS_VOLTAGE          = 2,

    // The USB protection front end reported a fault. The USB stack is torn
    // down while this stands, so the cable has to be disconnected.
    USB_PROTECTION       = 3,

    // Ambient (altimeter) too high to charge safely. Charging is inhibited.
    TEMPERATURE_AMBIENT  = 4,

    // Fuel-gauge die running hot relative to ambient, which points at the USB
    // cable or connector. Charging is inhibited.
    TEMPERATURE_CABLE    = 5,

    // Die critically hot while USB is present. A power-off would not stick
    // with valid VBUS, so charging is inhibited and the user is told to
    // disconnect instead.
    TEMPERATURE_CRITICAL = 6,

    // Die critically hot with no USB attached - the watch is powering itself
    // off. No recovery follows this one.
    TEMPERATURE_SHUTDOWN = 7,

    COUNT                = 8    // always last
};

#endif // __SYSTEM_ERROR_HPP
