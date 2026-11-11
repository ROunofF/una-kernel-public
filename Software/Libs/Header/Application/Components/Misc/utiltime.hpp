/**
 *******************************************************************************
 * @file    utiltime.hpp
 * @brief   A set of various utilities related to time
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#pragma once

#include <time.h>

#include <string>

namespace Misc {

    namespace Time {

    std::string time2str(time_t t);
    uint32_t    time2seconds(const char* str);
    time_t      makeTime(const char* str);
    time_t      makeTime(int y, int m, int d, int hh, int mm, int ss);

    }
}
