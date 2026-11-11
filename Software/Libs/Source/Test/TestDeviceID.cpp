#define LOG_MODULE_PRX      "TestDeviceID"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestDeviceID.hpp"
#include "MD5/md5.hpp"
#include "System.hpp"

#include <string.h>

////////////////////////////////////////////////////////////////////////////////
//// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Private code
////////////////////////////////////////////////////////////////////////////////

static uint32_t TestDeviceID_сalcDeviceId(const Driver::System::McuUid_t& id)
{
    MD5 md5;
    md5.update(id.bytes, sizeof(id.bytes));
    auto digest = md5.finalize();

    uint32_t v = 2166136261;           // FNV-1a
    for (size_t idx = 0; idx < sizeof(digest.bytes); ++idx) {
        v ^= digest.bytes[idx];
        v *= 16777619u;
    }

    v %= 1000000u;

    return v;
}

static void TestDeviceID_сalcDeviceIdStr(const Driver::System::McuUid_t& id, char out[7])
{
    sprintf(out, "%06lu", TestDeviceID_сalcDeviceId(id));
}

static void TestDeviceID_show(const Driver::System::McuUid_t& id)
{
    MD5 md5;
    md5.update(id.bytes, sizeof(id.bytes));
    auto digest = md5.finalize();

    char str[sizeof(id.bytes) * 2 + 1] = {0};
    for (uint8_t idx = 0; idx < sizeof(id.bytes); ++idx) {
        sprintf(&str[idx * 2], "%02X", id.bytes[idx]);
    }

    char hash[sizeof(digest.bytes) * 2 + 1] = {0};
    for (uint8_t idx = 0; idx < sizeof(digest.bytes); ++idx) {
        sprintf(&hash[idx * 2], "%02X", digest.bytes[idx]);
    }

    char mcuidstr[7] = {0};
    TestDeviceID_сalcDeviceIdStr(id, mcuidstr);

    LOG_INFO("ID        : %s\n", str);
    LOG_INFO("ID MD5    : %s\n", hash);
    LOG_INFO("ID string : %s/%s\n", Driver::System::GetDeviceIdStr(), mcuidstr);
}

static void TestDeviceID_show(const Driver::System::McuUid_t& original, uint8_t pos, uint8_t xorMask)
{
    assert(pos < ARRAY_SIZE(Driver::System::McuUid_t::bytes));

    Driver::System::McuUid_t id;
    memcpy(&id, &original, sizeof(Driver::System::McuUid_t));

    id.bytes[pos] ^= xorMask;

    TestDeviceID_show(id);
}

static uint32_t TestDeviceID_calc(const Driver::System::McuUid_t& original, uint8_t pos, uint8_t xorMask)
{
    assert(pos < ARRAY_SIZE(Driver::System::McuUid_t::bytes));

    Driver::System::McuUid_t id;
    memcpy(&id, &original, sizeof(Driver::System::McuUid_t));

    id.bytes[pos] ^= xorMask;

    return TestDeviceID_сalcDeviceId(id);
}

static bool TestDeviceID_checkDuplicates(const uint32_t* id, size_t n)
{
    bool hasDup = false;

    for (size_t i = 0; i < n; ++i) {
        for (size_t j = i + 1; j < n; ++j) {
            if (id[i] == id[j]) {
                hasDup = true;
                LOG_ERROR("Duplicate ID %06lu at indexes %u and %u\n",
                          (unsigned long)id[i],
                          (unsigned)i, (unsigned)j);
            }
        }
    }

    if (!hasDup) {
        LOG_INFO("No duplicates in %u values\n", (unsigned)n);
    }

    return hasDup;
}

static void TestDeviceID_print(const Driver::System::McuUid_t& original)
{
    uint32_t id[ARRAY_SIZE(original.bytes) * 8];

    for (uint8_t pos = 0; pos < ARRAY_SIZE(original.bytes); ++pos) {
        uint8_t mask = 0x01;
        for (uint8_t m = 0; m < 8; ++m) {
            id[pos * 8 + m] = TestDeviceID_calc(original, pos, mask);
            mask <<= 1;
        }
    }

    for (uint8_t idx = 0; idx < ARRAY_SIZE(id); ++idx) {
        LOG_INFO_WP("%06lu\n", id[idx]);
    }

    TestDeviceID_checkDuplicates(id, ARRAY_SIZE(id));
}

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

void TestDeviceID_execute()
{
    LOG_INFO("start\n");

    auto& id = Driver::System::GetMcuUid();

    TestDeviceID_show(id, ARRAY_SIZE(Driver::System::McuUid_t::bytes) - 1, 0x01);
    TestDeviceID_show(id, ARRAY_SIZE(Driver::System::McuUid_t::bytes) - 1, 0x02);
    TestDeviceID_show(id, 0, 0x01);
    TestDeviceID_show(id, 5, 0x80);

    TestDeviceID_print(id);
}
