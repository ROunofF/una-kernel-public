/**
 ******************************************************************************
 * @file    ProdTestBodySensorFactoryTest.сpp
 * @brief   Production test for the BodySensor Factory Test
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.BodySensor.FactoryTest"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"


#include "ProdTest/ProdTestBodySensorFactoryTest.hpp"
#include "Instances/InstancePAH8316LS.hpp"
#include "Gpio.hpp"
#include "SwTimer.hpp"

namespace ProdTest
{

BodySensorFactoryTest::BodySensorFactoryTest(const std::string& id)
    : Interface::IProdTest(id)
{
}

bool BodySensorFactoryTest::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test BodySensor (PAH8316LS)...\n");

    Hardware::PAH8316LSFactoryTester& ft = Instance::PAH8316LS::getInstance().getFactoryTester();

    if (!ft.checkID()) {
        slog.print("Failed to check ID");
        return false;
    }

    LOG_INFO("Bodysensor test...\n");

    return run(slog, ft);
}

bool BodySensorFactoryTest::run(StringLogger& slog, Hardware::PAH8316LSFactoryTester& ftester)
{
    bool LL_2LED       = ftester.demo_factory_mode(Light_Leak_Test_2_LED);
    bool CB_PCB_A_2LED = ftester.demo_factory_mode(Check_Brightness_PCB_Only_test_1_Side_A_2_LED);
    bool CB_PCB_B_2LED = ftester.demo_factory_mode(Check_Brightness_PCB_Only_test_2_Side_B_2_LED);
    bool INT_LINES     = ftester.demo_factory_mode(INT_TEST);
    bool CB_COV_A_2LED = ftester.demo_factory_mode(Check_Brightness_with_mechanical_cover_test_1_Side_A_2_LED);
    bool CB_COV_B_2LED = ftester.demo_factory_mode(Check_Brightness_with_mechanical_cover_test_2_Side_B_2_LED);

    slog.print("LL_2LED=%s",       LL_2LED       ? "PASS" : "FAIL");
    slog.print("CB_PCB_A_2LED=%s", CB_PCB_A_2LED ? "PASS" : "FAIL");
    slog.print("CB_PCB_B_2LED=%s", CB_PCB_B_2LED ? "PASS" : "FAIL");
    slog.print("INT_LINES=%s",     INT_LINES     ? "PASS" : "FAIL");
    slog.print("CB_COV_A_2LED=%s", CB_COV_A_2LED ? "PASS" : "FAIL");
    slog.print("CB_COV_B_2LED=%s", CB_COV_B_2LED ? "PASS" : "FAIL");

    return LL_2LED       &&
           CB_PCB_A_2LED &&
           CB_PCB_B_2LED &&
           INT_LINES     &&
           CB_COV_A_2LED &&
           CB_COV_B_2LED;
}

}   // namespace ProdTest
