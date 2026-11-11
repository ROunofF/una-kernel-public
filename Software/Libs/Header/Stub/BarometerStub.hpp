/**
 ******************************************************************************
 * @file    BarometerStub.hpp
 * @brief   Barometer stub.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef BAROMETER_STUB_HPP
#define BAROMETER_STUB_HPP

#include <cstdint>

#include "IBarometer.hpp"

namespace Stub
{

/**
 * @brief   Stands in for a barometer that is not present.
 *
 *          Refuses everything rather than pretending: init() and the
 *          streaming calls fail. The callback is accepted and never used.
 */
class Barometer : public Interface::IBarometer
{
public:
    Barometer() = default;
    ~Barometer() override = default;

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

#endif // BAROMETER_STUB_HPP
