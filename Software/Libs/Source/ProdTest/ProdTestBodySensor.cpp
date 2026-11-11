/**
 ******************************************************************************
 * @file    ProdTestBodySensor.сpp
 * @brief   Production test for the BodySensor.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.BodySensor"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger/Logger.h"


#include "ProdTest/ProdTestBodySensor.hpp"
#include "Instances/InstancePAH8316LS.hpp"
#include "CLI/CLIConsole.h"
#include "SwTimer.hpp"
#include "OS.hpp"

namespace ProdTest
{

BodySensor::BodySensor(const std::string& id)
    : Interface::IProdTest(id)
    , mTouched(false)
    , mDataValid(false)
    , mHr(0)
    , mTrustLevel(0)
{
}

bool BodySensor::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test BodySensor (PAH8316LS)...\n");
    LOG_INFO("Place your finger on the sensor... (wait 20 seconds)\n");

    Hardware::PAH8316LS& bodySensor = Instance::PAH8316LS::getInstance().getPAH8316LS();

    bodySensor.init();
    bodySensor.registerCallback(this);
    bodySensor.enable();

    bool ok = run(slog, bodySensor);

    bodySensor.disable();
    bodySensor.deinit();

    return ok;
}

bool BodySensor::run(StringLogger& slog, Hardware::PAH8316LS& bodySensor)
{
    if (bodySensor.getId() == 0) {
        LOG_ERROR("No BodySensor found\n");
        return false;
    }

    LOG_INFO("BodySensor ID: 0x%04X\n", bodySensor.getId());

    CLIConsole_print("Bodysensor test...\n");

    uint8_t counter   = 0;
    bool    isTouched = false;

    Driver::SwTimer t(SW_TIMER_SECONDS(40));

    while (true) {
        if (t.expired()) {
            return false;
        }

        if (mTouched && !isTouched) {
            LOG_INFO("Touch detected\n");
            isTouched = true;
            counter   = 0;
        }

        if (!mTouched && isTouched) {
            LOG_INFO("NO Touch detected\n");
            isTouched = false;
        }

        if (isTouched) {
            if (mDataValid) {
                mDataValid = false;

                if (mHr != 0 && mTrustLevel != 0) {
                    t.reset();

                    LOG_INFO("Heart rate: %d bpm, Trust level %d\n", (int)mHr, (int)mTrustLevel);

                    if (++counter >= 10) {
                        slog.print("Heart rate: %d bpm, Trust level %d", (int)mHr, (int)mTrustLevel);
                        return true;
                    }
                } else {
                    counter = 0;
                }
            }
        }

        OS::Delay(100);
    }
}

void BodySensor::onTouch(bool val)
{
    mTouched = val;
}

void BodySensor::onHeartRate(uint8_t hr, uint8_t trustLevel)
{
    mHr         = hr;
    mTrustLevel = trustLevel;
    mDataValid  = true;
}

}   // namespace ProdTest
