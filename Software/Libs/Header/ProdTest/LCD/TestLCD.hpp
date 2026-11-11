/**
 ******************************************************************************
 * @file    TestLCD.hpp
 * @brief   Test the LCD.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#pragma once

#include "Backlight.hpp"
#include "LS012B7DD06A.hpp"
#include "ProdTest/Core/StringLogger.hpp"

namespace ProdTest {

class TestLCD
{
public:
    TestLCD();
    ~TestLCD();

    bool init(StringLogger& slog);
    void deinit();

    void setColor(Interface::ILcd::Color color);

    void fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, Interface::ILcd::Color color);
    void paint();

private:
    void setPixelRotated(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color);

    Hardware::LS012B7DD06A mLcd;
    Backlight              mBacklight;
    uint8_t*               mBuff;
    uint32_t               mSize;
};

}   // namespace ProdTest
