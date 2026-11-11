/**
 ******************************************************************************
 * @file    SensorFloor.hpp
 * @brief   Virtual floor-counter sensor
 ******************************************************************************
 */

#ifndef SENSOR_FLOOR_HPP
#define SENSOR_FLOOR_HPP

#include "SensorLayer/Core/ISensor.hpp"
#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SensorLayer/Sensors/Floor/FloorCounter.hpp"
#include "OS.hpp"

#include <cstdint>

namespace Sensor
{

class Floor : public Interface::ISensor,
              public Sensor::ISensorDriverCtrl,
              public SDK::Interface::ISensorDataListener
{
public:
    Floor();
    ~Floor() override = default;

    Sensor::Driver& getDriver();

    // ISensorDriverCtrl
    float       sdcStart(Sensor::Driver* driver, float period)        override;
    void        sdcStop(Sensor::Driver* driver)                       override;
    const char* sdcGetDescription(Sensor::Driver* driver)             override;
    void        sdcNewConnection(Sensor::Driver* driver)              override;

    // ISensor
    void sensorRefresh() override;

    // ISensorDataListener
    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override;

private:
    struct Counters {
        uint32_t up   = 0;
        uint32_t down = 0;

        bool operator==(const Counters& other) const noexcept
        {
            return (up == other.up) && (down == other.down);
        }

        bool operator!=(const Counters& other) const noexcept
        {
            return !(*this == other);
        }

        Counters& operator=(const Counters& other) noexcept = default;
    };

    void publishData(const Counters& counters);

    static constexpr uint16_t mPressurePeriod = 200;  // In ms

    Sensor::Connection   mDriverStepDetector;
    Sensor::Connection   mDriverPressure;
    Sensor::Driver       mDriver;
    Sensor::FloorCounter mFloorCounter;
    Counters             mCounters;
    uint32_t             mTimestamp = 0;
    bool                 mDirty     = false;
    OS::Mutex            mDataMutex;
}; // class Floor

} // namespace Sensor

#endif // SENSOR_FLOOR_HPP
