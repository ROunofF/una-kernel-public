#define LOG_MODULE_PRX      "TestEMMC"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Test/TestEMMC.hpp"
#include "OS.hpp"
#include "Flash.hpp"
#include "Gpio.hpp"
#include "File.hpp"

#include <cassert>


void TestEMMC_execute()
{
    // disable backlight
    Driver::Gpio::GetInstance(Driver::Gpio::Pin::BACKLIGHT_ON).init(Interface::IGpo::Type::OD, true);
    const char *path = "2:/settings.json";


    LOG_INFO("start\n");
    Flash &flash   = Flash::GetInstance();

    static const size_t skBuffSize = 4096;
    static uint8_t buffer[skBuffSize];



    flash.powerModeAuto();

    FS::File file { };
    file.setPath(path);

    LOG_INFO_WP("////////////////////////////////////////////////////////////\n");
    LOG_INFO_WP("//// TESTING EMMC\n");
    LOG_INFO_WP("////////////////////////////////////////////////////////////\n");


    while (1) {

#if 0
        uint32_t testAddr = 0x12F6EA00;
        LOG_INFO("Read sector at 0x%08X\n", testAddr);
        if (!flash.memRead(testAddr, buffer, skBuffSize)) {
            LOG_INFO("Retry\n");
            flash.memRead(testAddr, buffer, skBuffSize);
        }
#else
        LOG_INFO("Read file\n");
        size_t br = 0;
        if (file.open()) {

            if (!file.read(reinterpret_cast<char*>(buffer), skBuffSize, br)) {
                LOG_ERROR("Failed to read file %s\n");
            }
            file.close();
        } else {
            LOG_ERROR("Failed to open file %s\n");
        }
        LOG_INFO("Done. %u bytes\n", br);

#endif
        OS::Delay(2500);
    }

}
