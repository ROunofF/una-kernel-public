/**
 ******************************************************************************
 * @file    SensorConnection.cpp
 * @brief   Connection to the Sensor Driver
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Sensor.Connect"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "SensorLayer/Core/SensorConnection.hpp"
#include "SensorLayer/Core/SensorManager.hpp"

namespace Sensor {

/**
 * @brief Construct a connection wrapper for a sensor driver.
 *
 * Resolves the underlying sensor driver via
 * `kernel->sensorManager.getDefaultSensor(id)` and stores connection
 * parameters for subsequent @ref connect() calls.
 *
 * @param id       Sensor type identifier to resolve the default driver.
 * @param listener Pointer to a data listener (must remain valid while connected).
 * @param period   Desired sampling/update period (units as defined by the driver; commonly seconds).
 * @param latency  Maximum report latency/batching tolerance (units as defined by the driver; commonly milliseconds).
 * @pre `kernel` must be initialized.
 * @note Use @ref isValid() to check that an underlying driver was found.
 */
Connection::Connection(SDK::Sensor::Type                    id,
                       SDK::Interface::ISensorDataListener* listener,
                       float                                period,
                       uint32_t                             latency)
    : mID(id)
    , mDriver(nullptr)
    , mListener(listener)
    , mPeriod(period)
    , mLatency(latency)
    , mIsConnected(false)
{}

/**
 * @brief Construct a connection wrapper using an explicit sensor driver.
 *
 * Stores the provided driver and connection parameters for subsequent @ref connect() calls.
 *
 * @param driver   Pointer to the sensor driver to use.
 * @param listener Pointer to a data listener (must remain valid while connected).
 * @param period   Desired sampling/update period (units as defined by the driver; commonly seconds).
 * @param latency  Maximum report latency/batching tolerance (units as defined by the driver; commonly milliseconds).
 *
 * @pre `driver` must be valid and compatible with the listener.
 */
Connection::Connection(Sensor::Driver*                      driver,
                       SDK::Interface::ISensorDataListener* listener,
                       float                                period,
                       uint32_t                             latency)
    : mID(SDK::Sensor::Type{})
    , mDriver(driver)
    , mListener(listener)
    , mPeriod(period)
    , mLatency(latency)
    , mIsConnected(false)
{}

Connection::~Connection()
{
    disconnect();
}

/**
 * @brief Check whether the underlying driver has been resolved.
 * @return `true` if a driver pointer is available, otherwise `false`.
 */
bool Connection::isValid()
{
    return (mDriver != nullptr);
}

/**
 * @brief Connect to the driver using the stored period and latency.
 *
 * Delegates to the driver's `connect(listener, userApp, period, latency)`.
 *
 * @return `true` on successful connection, `false` if no driver is available or the driver rejects the request.
 * @see connect(float,uint32_t)
 */
bool Connection::connect()
{
    if (!isValid()) {
        mDriver = Sensor::Manager::getInstance().getDefaultSensor(mID);

        if (!mDriver) {
            LOG_WARNING("failed to get default driver for ID 0x%08X\n", mID);
            return false;
        }
    }

    return (mIsConnected = mDriver->connect(mListener, mPeriod, mLatency));
}

/**
 * @brief Update period/latency and connect to the driver.
 *
 * Stores the provided parameters and then calls the parameterless @ref connect().
 *
 * @param period   Desired sampling/update period (units as defined by the driver).
 * @param latency  Maximum report latency/batching tolerance (units as defined by the driver).
 * @return `true` on successful connection, otherwise `false`.
 */
bool Connection::connect(float period, uint32_t latency)
{
    mPeriod  = period;
    mLatency = latency;

    return connect();
}

bool Connection::isConnected()
{
    return mIsConnected;
}

/**
 * @brief Disconnect the listener from the underlying driver.
 *
 * Safe to call even if the connection is not valid or already disconnected.
 * If no driver is available, the call is a no-op.
 */
void Connection::disconnect()
{
    if (!isValid()) {
        return;
    }

    if (!mIsConnected) {
        return;
    }

    mDriver->disconnect(mListener);

    mIsConnected = false;
}

/**
 * @brief Check if the specified driver matches the connected one.
 *
 * @details
 * Compares the given driver pointer with the internal driver instance
 * associated with this connection. Returns true only if both pointers
 * are valid and refer to the same ISensorDriver object.
 *
 * @param  driver Pointer to a sensor driver to compare against.
 * @return true  If the specified driver is the same as the one managed by this connection.
 * @return false If either pointer is null or they do not match.
 */
bool Connection::matchesDriver(uint16_t handle)
{
    if (!isValid()) {
        return false;
    }

    if (!mIsConnected) {
        return false;
    }

    return (mDriver->getHandle() == handle);
}

} // namespace SDK::Sensors
