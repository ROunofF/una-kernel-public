/**
 ******************************************************************************
 * @file    SensorManager.hpp
 * @brief   SensorManager class
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __SENSOR_MANAGER_HPP
#define __SENSOR_MANAGER_HPP

#include "SensorLayer/Core/SensorDriver.hpp"
#include "SensorLayer/Core/ISensor.hpp"
#include "OS.hpp"

#include "etl/vector.h"

namespace Sensor
{
    class Manager {
    public:
        /// Upper bound on registered drivers. Every driver this kernel can
        /// have is registered once, from Instance::SensorLayer::init(): 34
        /// of them with every hardware block present, plus the event-based
        /// test probe. The margin above that is deliberate headroom for a
        /// sensor added without revisiting this line - regDriver refuses
        /// past it rather than growing, so what the table costs in RAM is a
        /// fact about the build and not about what the heap allowed.
        static constexpr size_t skMaxDrivers = 40;

        /// One entry per ISensor that has at least one live listener. A
        /// driver registers its sensor on the first connect, so this can
        /// never exceed the number of drivers.
        static constexpr size_t skMaxSensors = skMaxDrivers;

        using DriverList = etl::vector<Sensor::Driver*, skMaxDrivers>;
        using SensorList = etl::vector<Interface::ISensor*, skMaxSensors>;
        using HandleList = etl::vector<uint16_t, skMaxDrivers>;

        static Manager& getInstance();

        bool regDriver(Sensor::Driver* driver);
        bool isDriverRegistered(Sensor::Driver* driver) const;
        bool isDriverRegistered(SDK::Sensor::Type type) const;

        void updatePeriod();

        // Wake the refresh thread immediately. Event-based sensors call this
        // from their producer callback so a pending sample is delivered
        // without waiting for the next poll tick.
        void wake();

        void regSensor(Interface::ISensor* sensor);
        void unRegSensor(Interface::ISensor* sensor);

        // Disconnect a listener from every registered driver. Used by the app
        // terminate path so a crashing/force-killed app can never leave a
        // sensor powered / subscription registered. Safe to call for a
        // listener that was never connected: Driver::disconnect no-ops when
        // the listener is not present.
        void disconnectListener(SDK::Interface::ISensorDataListener* listener);

        Sensor::Driver*              getDefaultSensor(SDK::Sensor::Type type) const;
        uint16_t                     getDefaultHandle(SDK::Sensor::Type type) const;

        // Returned by value, and the value now carries its own storage: a
        // HandleList is skMaxDrivers entries wide whatever it holds. That
        // comes out of the caller's stack frame rather than the heap, so it
        // is cheap to return but not free to keep - take one, read it, let
        // it go.
        HandleList                   getHandleList(SDK::Sensor::Type type) const;

        Sensor::Driver*              getDriverByHandle(uint32_t handle) const;

    private:
        // Lower bound for the Manager refresh tick. Without it a sub-millisecond
        // sensor refresh period truncates to 0 and the thread would busy-poll.
        static constexpr uint32_t sMinPeriodMs = 10;

        // Fallback refresh tick when no periodic sensor is active. Event-based
        // sensors wake the thread on demand, so only a slow safety poll remains.
        static constexpr uint32_t sIdlePeriodMs = 1000;

        Manager();

        void updatePeriodNoLock();
        void thread();

        DriverList                       mDrivers;
        SensorList                       mSensors;
        OS::Thread<Manager, 8*1024>      mThread;
        OS::Mutex                        mMutex;
        OS::Semaphore                    mSemaphore;
        uint32_t                         mPeriodTicks;
    };

} /* namespace Sensor */

#endif /* __SENSOR_MANAGER_HPP */
