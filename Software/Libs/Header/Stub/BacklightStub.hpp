/**
 ******************************************************************************
 * @file    BacklightStub.hpp
 * @brief   Backlight stub.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __BACKLIGHT_STUB_HPP
#define __BACKLIGHT_STUB_HPP

#include <cstdint>
#include <cstdbool>

#include "IBacklight.hpp"

namespace Stub
{

class Backlight : public Interface::IBacklight {

public:

    Backlight() = default;

    virtual ~Backlight() = default;

    bool init() { return false; }

    bool deinit() { return true; }

    virtual bool on(uint32_t timeout = 0) override;

    virtual bool off() override;

    virtual bool isOn() override;

protected:

private:

};

} /* namespace Stub */

#endif /* __BACKLIGHT_STUB_HPP */
