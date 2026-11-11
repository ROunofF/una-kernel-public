#define LOG_MODULE_PRX      "TestMD5"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestMD5.hpp"
#include "MD5/md5.hpp"

#include <string.h>

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

void TestMD5_execute()
{
    LOG_INFO("start\n");

	MD5::Digest digest = MD5::calc("test md5 library");

    LOG_INFO_DUMP(digest.bytes, ARRAY_SIZE(digest.bytes));

    char str[33] = {0};
    for (uint8_t i = 0; i < 16; ++i) {
        sprintf(&str[i * 2], "%02X", digest.bytes[i]);
    }
    
    if (strcmp("D3870F446328B5895C57D1C6B9E0945A", str) == 0) {
        LOG_INFO("OK\n");
    } else {
        LOG_ERROR("MD5 is incorrect\n");
    }    
}
