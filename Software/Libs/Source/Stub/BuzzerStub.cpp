/**
 ******************************************************************************
 * @file    BuzzerStub.cpp
 * @brief   Buzzer interface stub.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Stub/BuzzerStub.hpp"

#define LOG_MODULE_PRX      "BuzzerStub"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

bool Stub::Buzzer::play()
{
    LOG_DEBUG("called\n");
    return false;
}

bool Stub::Buzzer::play(Note note)
{
    LOG_DEBUG("called\n");
    return false;
}

bool Stub::Buzzer::play(const Note melody[], uint8_t len)
{
    LOG_DEBUG("called\n");
    return false;
}

bool Stub::Buzzer::isPlaying()
{
    LOG_DEBUG("called\n");
    return false;
}

void Stub::Buzzer::stop()
{
    LOG_DEBUG("called\n");
}
