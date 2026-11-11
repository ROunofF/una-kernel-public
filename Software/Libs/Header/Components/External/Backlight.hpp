/**
 ******************************************************************************
 * @file    Backlight.hpp
 * @brief   Backlight component.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __COMPONENT_BACKLIGHT_HPP
#define __COMPONENT_BACKLIGHT_HPP

#include <cstdint>
#include <cstdbool>

#include "IBacklight.hpp"
#include "IGpio.hpp"
#include "OS.hpp"

class Backlight : public Interface::IBacklight {

public:

    /**
     * @brief   Constructor.
     * @param   pin: backlight pin.
     */
    Backlight(Interface::IGpo &pin) : mPin(pin)
    {}

    /**
     * @brief   Destructor.
     */
    virtual ~Backlight() = default;

    /**
     * @copydoc Interface::IBacklight::init()
     */
    virtual bool init() override;

    /**
     * @copydoc Interface::IBacklight::deinit()
     */
    virtual bool deinit() override;

    /**
     * @copydoc Interface::IBacklight::on()
     */
    virtual bool on(uint32_t timeout = 0) override;

    /**
     * @copydoc Interface::IBacklight::off()
     */
    virtual bool off() override;

    /**
     * @copydoc Interface::IBacklight::isOn()
     */
    virtual bool isOn() override;

protected:


private:

    // LCD backlight pin
    Interface::IGpo &mPin;

    // Component init flag
    bool mIsInit = false;

    // Auto off timer
    OS::Timer<Backlight> mTimer;

    void timCallback(void *arg);

};

#endif /* __COMPONENT_BACKLIGHT_HPP */
