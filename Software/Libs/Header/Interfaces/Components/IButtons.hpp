/**
 ******************************************************************************
 * @file    IButtons.hpp
 * @brief   Buttons Interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __INTERFACE_I_BUTTONS_HPP
#define __INTERFACE_I_BUTTONS_HPP

#include <cstdint>

namespace Interface
{

/**
 * @brief Buttons Interface.
 */
class IButtons {

public:

//          UP      Top Left        L1      SW1
//          DOWN    Bottom Left     L2      SW3
//          SELECT  Top Right       R1      SW2 (PWR_ON_1V8_L)
//          BACK    Bottom Right    R2      SW4

    /**
     * @brief Available buttons.
     */
    enum class Id : uint8_t {
        SW1 = 0,    // L1
        SW2,        // R1
        SW3,        // L2
        SW4,        // R2

        MAX_NUM      // Not a valid parameter
    };

    /**
     * @brief Individual event codes.
     */
    enum class Code : uint8_t {
        // One button click
        L1_CLICK = '1',
        L2_CLICK = '2',
        R1_CLICK = '3',
        R2_CLICK = '4',

        // One button long press
        L1_LONG_PRESS = '!',
        L2_LONG_PRESS = '@',
        R1_LONG_PRESS = '#',
        R2_LONG_PRESS = '$',

        // One button down, and up again. Sent as they happen, so a screen can
        // act while a button is still held -- a list that scrolls on for as
        // long as it is, say -- rather than only on the click that a release
        // under half a second turns into. Unlike a click these are not
        // withheld when a second button joins in: whether a press is part of a
        // chord is not known until it is, and a release always follows.
        //
        // The digits carry the press and their shifted forms the release, the
        // way the clicks and their long presses already pair up.
        L1_PRESS = '5',
        L2_PRESS = '6',
        R1_PRESS = '7',
        R2_PRESS = '8',

        L1_RELEASE = '%',
        L2_RELEASE = '^',
        R1_RELEASE = '&',
        R2_RELEASE = '*',

        // Two buttons press
        L1_L2_PRESS = 'q',
        R1_R2_PRESS = 'e',
        L1_R1_PRESS = 'a',
        L2_R2_PRESS = 's',
        L1_R2_PRESS = 'z',
        R1_L2_PRESS = 'w',
    };

    /**
     * @brief Buttons Callback interface
     */
    class Callback {
    public:
        virtual void onPress(Interface::IButtons::Id id) { }
        virtual void onRelease(Interface::IButtons::Id id) { }
        virtual void onClick(Interface::IButtons::Id id) { }
        virtual void onLongPress(Interface::IButtons::Id id) { }
        virtual void onHold_1s(Interface::IButtons::Id id) { }
        virtual void onHold_5s(Interface::IButtons::Id id) { }
        virtual void onHold_10s(Interface::IButtons::Id id) { }

        virtual void onCode(Interface::IButtons::Code code) { }

    protected:

        /**
         * @brief Destructor.
         */
        virtual ~Callback() = default;
    };

    /**
     * @brief   Attach a callback for buttons events.
     * @note    It is allowed attach multiple callbacks.
     * @param   pCallback: Pointer to the callback interface.
     */
    virtual void attachCallback(Interface::IButtons::Callback *pCallback) = 0;

    /**
     * @brief   Detach the currently attached buttons callback.
     * @param   pCallback: Pointer to the callback interface to be detached.
     */
    virtual void detachCallback(Interface::IButtons::Callback *pCallback) = 0;

    /**
     * @brief   Get Buttons state.
     * @param   id: Button ID.
     * @retval  'true' pressed, 'false' released.
     */
    virtual bool get(Interface::IButtons::Id id) = 0;

    /**
     * @brief   Get Buttons time in state.
     * @param   id: Button ID.
     * @retval  Time in current state in ms.
     */
    virtual uint32_t getTime(Interface::IButtons::Id id) = 0;

protected:

    /**
     * @brief Destructor.
     */
    virtual ~IButtons() = default;

};

} /* namespace Interface */

#endif /* __INTERFACE_I_BUTTONS_HPP */
