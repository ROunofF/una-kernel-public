/**
 ******************************************************************************
 * @file    LcdStub.hpp
 * @brief   LCD stub.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __STUB_LCD_HPP
#define __STUB_LCD_HPP

#include <cstdint>
#include <cstdbool>

#include "ILcd.hpp"

namespace Stub
{

class Lcd : virtual public Interface::ILcd
{

public:

    Lcd() = default;

    virtual ~Lcd() = default;

    virtual bool init() override { return false; }

    virtual bool deinit() override { return true; }

    virtual bool enable() override { return false; }

    virtual bool disable() override { return true; }

    virtual bool isEnabled() override { return false; }

    virtual void setResolution(int16_t width, int16_t height) override {}

    virtual void getResolution(int16_t &width, int16_t &height) override { width = 0; height = 0; }

    virtual uint8_t getDisplayColorDepth() override { return 0; }

    virtual void write(const uint8_t *pBuf) override {}

protected:

private:

};

} /* namespace Stub */

#endif /* __STUB_LCD_HPP */
