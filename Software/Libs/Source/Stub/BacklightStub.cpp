/**
 ******************************************************************************
 * @file    BacklightStub.cpp
 * @brief   Backlight interface stub.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Stub/BacklightStub.hpp"

#define LOG_MODULE_PRX      "BacklightStub"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

bool Stub::Backlight::on(uint32_t timeout)
{
    LOG_DEBUG("called, timeout = %u\n", timeout);
    return true;
}

bool Stub::Backlight::off()
{
    LOG_DEBUG("called\n");
    return true;
}

bool Stub::Backlight::isOn()
{
    LOG_DEBUG("called\n");
    return false;
}
