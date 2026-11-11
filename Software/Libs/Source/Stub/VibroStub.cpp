/**
 ******************************************************************************
 * @file    VibroStub.cpp
 * @brief   Vibro interface stub.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Stub/VibroStub.hpp"

#define LOG_MODULE_PRX      "VibroStub"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

void Stub::Vibro::play(uint8_t effect)
{
    LOG_DEBUG("called, effect = %u\n", effect);
}

void Stub::Vibro::play(const Note melody[], uint8_t len, uint8_t loop)
{
    LOG_DEBUG("called, melody len = %u\n", len);
}

bool Stub::Vibro::isPlaying()
{
    LOG_DEBUG("called\n");
    return false;
}

void Stub::Vibro::stop()
{
    LOG_DEBUG("called\n");
}
