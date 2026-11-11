/**
 ******************************************************************************
 * @file    ImuStub.hpp
 * @brief   IMU stub for a board without one.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef IMU_STUB_HPP
#define IMU_STUB_HPP

#include <cstdint>

#include "IImu.hpp"

namespace Stub
{

/**
 * @brief   Stands in for an IMU that is not present.
 *
 *          Refuses everything rather than pretending: init() and every
 *          request fail. The callback is accepted and never used.
 */
class Imu : public Interface::IImu
{

public:

    Imu() = default;

    ~Imu() override = default;

    void setSensorCallback(SensorCallback*) override {}

    bool init() override                    { return false; }
    bool deinit() override                  { return false; }
    bool isInitialized() const override     { return false; }

    bool resetStepCounter() override        { return false; }

    bool setFifoLatency(FifoChannel, uint32_t) override { return false; }

    bool enableAccel(uint32_t) override     { return false; }
    bool disableAccel() override            { return false; }
    uint16_t getAccelRange() const override { return 0; }
    void setAccelRange(uint16_t) override   {}
    float getAccelScale() const override    { return 0.0f; }

    bool enableGyro(uint32_t) override      { return false; }
    bool disableGyro() override             { return false; }
    uint16_t getGyroRange() const override  { return 0; }
    void setGyroRange(uint16_t) override    {}
    float getGyroScale() const override     { return 0.0f; }

    bool enableStepCounter() override       { return false; }
    bool disableStepCounter() override      { return false; }

    bool enableStepDetector() override      { return false; }
    bool disableStepDetector() override     { return false; }

    bool enableActivity() override          { return false; }
    bool disableActivity() override         { return false; }

    bool enableWristWakeup() override       { return false; }
    bool disableWristWakeup() override      { return false; }

    bool enableAnyMotion(bool) override     { return false; }
    bool disableAnyMotion() override        { return false; }

    bool enableNoMotion(bool) override      { return false; }
    bool disableNoMotion() override         { return false; }

    bool enableSigMotion(bool) override     { return false; }
    bool disableSigMotion() override        { return false; }

    bool disableAll() override              { return false; }

};

} // namespace Stub

#endif // IMU_STUB_HPP
