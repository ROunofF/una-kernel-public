/**
 ******************************************************************************
 * @file    IBodySensor.hpp
 * @brief   Body sensor Interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef UNA_WATCH_IBODYSENSOR_HPP
#define UNA_WATCH_IBODYSENSOR_HPP

#include <cstdint>

namespace Interface {

/**
 * @brief Body sensor Interface.
 */
class IBodySensor
{
public:

    /**
     * @brief Callback interface for body sensor events.
     *
     * Provides notifications for touch events and heart rate measurements.
     */
    class Callback {
    public:
        /**
         * @brief Notification about touch sensor state change.
         * @param val Current touch state (true = touched).
         */
        virtual void onTouch(bool val) {};

        /**
         * @brief Notification about a heart rate measurement.
         * @param hr          Measured heart rate (BPM).
         * @param trustLevel  Quality/trust level of the measurement.
         */
        virtual void onHeartRate(uint8_t hr, uint8_t trustLevel) {};

    protected:
        /**
         * @brief Destructor.
         */
        virtual ~Callback() = default;
    };

    /**
     * @brief Register a body sensor callback.
     * @param pCallback Pointer to a Callback interface implementation.
     */
    virtual void registerCallback(Interface::IBodySensor::Callback *pCallback) = 0;



    /**
     * @brief Represents a single 3-axis accelerometer reading.
     */
    struct __attribute__((packed)) AccSample {
        int16_t x; ///< Acceleration on X axis (raw units)
        int16_t y; ///< Acceleration on Y axis (raw units)
        int16_t z; ///< Acceleration on Z axis (raw units)
    };

    /**
     * @brief Callback interface for retrieving acceleration data for HR/SPO2 algorithms.
     *
     * Implementations should provide access to buffered acceleration samples
     * within a requested timestamp range.
     */
    class AccRequestCallback {
    public:
        /**
         * @brief Retrieve acceleration samples for a given timestamp range.
         *
         * @param startTimestamp  Lower bound of timestamp (inclusive).
         * @param endTimestamp    Upper bound of timestamp (exclusive).
         * @param minSamples      Minimum number of samples required.
         * @param maxSamples      Maximum number of samples allowed (available buffer size).
         * @param buff            Pointer to the output buffer for samples.
         * @param count           On output, contains the number of samples written.
         */
        virtual void onAccRequest(uint32_t startTimestamp, uint32_t endTimestamp,
                uint32_t minSamples, uint32_t maxSamples,
                AccSample *buff, uint32_t &count) = 0;

    protected:
        /**
         * @brief Destructor.
         */
        virtual ~AccRequestCallback() = default;
    };

    /**
     * @brief Register callback for retrieving acceleration data.
     * @param pCallback Pointer to an AccRequestCallback implementation.
     */
    virtual void registerAccReqCallback(Interface::IBodySensor::AccRequestCallback *pCallback) {}



protected:

    /**
     * @brief Destructor.
     */
    virtual ~IBodySensor() = default;
};

} // namespace Interface

#endif //UNA_WATCH_IBODYSENSOR_HPP
