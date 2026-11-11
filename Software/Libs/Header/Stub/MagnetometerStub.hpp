/**
 ******************************************************************************
 * @file    MagnetometerStub.hpp
 * @brief   Magnetometer stub.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef MAGNETOMETER_STUB_HPP
#define MAGNETOMETER_STUB_HPP

#include <cstdint>

#include "IMagnetometer.hpp"

namespace Stub
{

/**
 * @brief   Stands in for a magnetometer that is not present.
 *
 *          Refuses everything rather than pretending: init() and the
 *          streaming calls fail. The callback is accepted and never used.
 */
class Magnetometer : public Interface::IMagnetometer
{

public:

    Magnetometer() = default;

    ~Magnetometer() override = default;

    bool init() override                       { return false; }
    void deinit() override                     {}
    bool isInit() const override               { return false; }
    void setDataCallback(DataCallback*) override {}

    bool requestStart(float) override          { return false; }
    bool requestStop() override                { return false; }
    bool isRunning() const override            { return false; }

    /// Both zero: there is no ladder, and a plausible range would mislead.
    float getMaxSampleRateHz() const override  { return 0.0f; }
    float getMinSampleRateHz() const override  { return 0.0f; }

};

} // namespace Stub

#endif /* MAGNETOMETER_STUB_HPP */
