/**
 ******************************************************************************
 * @file    GpsSubSensorLocation.hpp
 * @brief   SubSensor for the GPS Location
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __GPS_SUB_SENSOR_LOCATION_HPP
#define __GPS_SUB_SENSOR_LOCATION_HPP

#include "SensorLayer/Core/ISubSensor.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "Filters/SimpleLPF.hpp"
#include "SensorLayer/Core/SourceTick.hpp"
#include "OS.hpp"
#include "IGps.hpp"

#include <cstdint>

using namespace Interface;

namespace Sensor
{
    class GpsSubSensorLocation : public Sensor::ISubSensor,
                                 public Interface::ISensor
    {
    public:
        GpsSubSensorLocation(Sensor::ISensorDriverCtrl& ctrl);

        //// ISubSensor
        Sensor::Driver& getDriver()                override;
        bool            isActive()                 override;
        float           start(float period)        override;
        void            stop()                     override;
        float           updatePeriod(float period) override;
        float           getMinPeriod()             override;
        float           getCurrentPeriod()         override;
        const char*     getDescription()           override;

        //// ISensor
        void sensorRefresh() override;

        void setLocation(const Interface::IGps::LocationInfo& location);

    private:
        float deg2rad(float degrees)
        {
            // Conversion factor from degrees to radians.
            return static_cast<float>((degrees * M_PI) / 180.0);
        }

        // This method uses a simplified (Euclidean) flat-earth formula assuming
        // small distances, which is appropriate for local map rendering purposes.
        float distanceFlatEarth(const IGps::LocationInfo& p1, const IGps::LocationInfo& p2)
        {
            /// Approximate meters per degree of latitude.
            static constexpr float kMetersPerDegree = 111320.0f;

            float dx = (p2.lon - p1.lon) * std::cos(deg2rad((p1.lat + p2.lat) * 0.5f));
            float dy = (p2.lat - p1.lat);
            return kMetersPerDegree * std::hypot(dx, dy);
        }

        float distanceFlatEarth(const IGps::LocationInfo& p1, float lat, float lon)
        {
            IGps::LocationInfo p2{};
            p2.lat = lat;
            p2.lon = lon;

            return distanceFlatEarth(p1, p2);
        }

        Sensor::Driver                   mDriver;
        Sensor::SourceTick               mTick;
        Filter::SimpleLPF                mFilterLat;
        Filter::SimpleLPF                mFilterLon;
        Filter::SimpleLPF                mFilterAlt;
        IGps::LocationInfo               mPoint0;
        OS::Queue<IGps::LocationInfo, 5> mPoints;

        // Last fix accepted by the plausibility gate (raw, pre-filter) plus the
        // time it was accepted, used to reject teleport-sized jumps caused by a
        // single corrupted coordinate before it reaches the LPF.
        IGps::LocationInfo               mLastGood;
        uint32_t                         mLastGoodMs;
        uint8_t                          mRejectStreak;
        bool                             mHasLastGood;

    }; /* class GpsSubSensorLocation */

} // namespace Sensor

#endif /* __GPS_SUB_SENSOR_LOCATION_HPP */
