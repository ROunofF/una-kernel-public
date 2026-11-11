/**
 ******************************************************************************
 * @file    TestLCD.сpp
 * @brief   Test the LCD.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "TestLCD"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/LCD/TestLCD.hpp"
#include "Gpio.hpp"
#include "ScopeGuard.hpp"

#include <algorithm>
#include <new>   // std::nothrow

namespace ProdTest
{

TestLCD::TestLCD()
    : mLcd()
    , mBacklight(Driver::Gpio::GetInstance( Driver::Gpio::Pin::BACKLIGHT_ON))
    , mBuff(nullptr)
    , mSize(0)
{}

TestLCD::~TestLCD()
{
    deinit();
}

bool TestLCD::init(StringLogger& slog)
{
    if (!mBacklight.init()) {
        LOG_ERROR("%s\n", slog.print("Failed to init the Backlight"));
        return false;
    }

    mBacklight.on();

    auto blGuard = MakeScopeGuard([this]() {
        mBacklight.off();
        mBacklight.deinit();
    });

    if (!mLcd.init()) {
        LOG_ERROR("%s\n", slog.print("Failed to init the LCD"));
        return false;
    }

    mLcd.enable();

    auto lcdGuard = MakeScopeGuard([&]() {
        mLcd.disable();
        mLcd.deinit();
    });

    int16_t width;
    int16_t height;

    mLcd.getResolution(width, height);

    mSize = width * height;
    mBuff = new (std::nothrow) uint8_t[mSize];

    if (!mBuff) {
        LOG_ERROR("%s\n", slog.print("No memory"));
        return false;
    }

    blGuard.dismiss();
    lcdGuard.dismiss();

    return true;
}

void TestLCD::deinit()
{
    if (!mBuff) {
        return;
    }

    delete [] mBuff;

    mBuff = nullptr;

    mBacklight.off();
    mBacklight.deinit();

    mLcd.disable();
    mLcd.deinit();
}

void TestLCD::setColor(Interface::ILcd::Color color)
{
    uint8_t c = static_cast<uint8_t>(color);

    for (uint32_t i = 0; i < mSize; i++) {
        mBuff[i] = c;
    }

    paint();
}

void TestLCD::fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, Interface::ILcd::Color color)
{
    int16_t width;
    int16_t height;

    mLcd.getResolution(width, height);

    const int16_t top = std::clamp(
        static_cast<int16_t>(height * (y / 100.0f)),
        static_cast<int16_t>(0),
        static_cast<int16_t>(height - 1));

    const int16_t left = std::clamp(
        static_cast<int16_t>(width * (x / 100.0f)),
        static_cast<int16_t>(0),
        static_cast<int16_t>(width - 1));

    const int16_t bottom = std::clamp(
        static_cast<int16_t>(top + height * (h / 100.0f)),
        static_cast<int16_t>(0),
        height);

    const int16_t right = std::clamp(
        static_cast<int16_t>(left + width * (w / 100.0f)),
        static_cast<int16_t>(0),
        width);

    const uint8_t c = static_cast<uint8_t>(color);

    // Write to the buffer with rotation 90 CW
    for (int16_t yPos = top; yPos < bottom; ++yPos) {
        for (int16_t xPos = left; xPos < right; ++xPos) {
            setPixelRotated(xPos, yPos, width, height, c);
        }
    }
}

void TestLCD::paint()
{
    mLcd.write(mBuff);
}

void TestLCD::setPixelRotated(int16_t x, int16_t y, uint8_t w, uint8_t h, uint8_t color)
{
    const int16_t bufX = y;
    const int16_t bufY = h - 1 - x;

    if ((bufX >= 0) && (bufX < w) &&
        (bufY >= 0) && (bufY < h)) {
        mBuff[(bufY * w) + bufX] = color;
    }
}

}   // namespace ProdTest
