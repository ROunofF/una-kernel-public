/**
 ******************************************************************************
 * @file    SensorConnection.hpp
 * @brief   Connection to the Sensor Driver
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#pragma once

#include "SensorLayer/Core/SensorDriver.hpp"
#include "SDK/SensorLayer/SensorTypes.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"

namespace Sensor {

    class Connection {
    public:
        Connection(SDK::Sensor::Type                    id,
                   SDK::Interface::ISensorDataListener* listener,
                   float                                period  = 0,
                   uint32_t                             latency = 0);

        Connection(Sensor::Driver*                      driver,
                   SDK::Interface::ISensorDataListener* listener,
                   float                                period  = 0,
                   uint32_t                             latency = 0);

        ~Connection();

        bool isValid();

        bool connect();
        bool connect(float period, uint32_t latency = 0);

        bool isConnected();

        void disconnect();
        
        bool matchesDriver(uint16_t handle);

    protected:
        SDK::Sensor::Type                    mID;
        Sensor::Driver*                      mDriver;
        SDK::Interface::ISensorDataListener* mListener;
        float                                mPeriod;
        uint32_t                             mLatency;
        bool                                 mIsConnected;
    };

} // namespace SDK::Sensor
