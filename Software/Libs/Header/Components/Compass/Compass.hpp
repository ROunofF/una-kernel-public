/**
 ******************************************************************************
 * @file    Compass.hpp
 * @brief   A live bearing of 12 o'clock, from the field and the accelerometer.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef COMPONENT_COMPASS_HPP
#define COMPONENT_COMPASS_HPP

#include <cstdint>

#include "Compass/GravityTracker.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"

/**
 * @brief   Subscribes to the corrected field and the accelerometer while it
 *          runs, and reports a bearing for every field sample.
 *
 *          Gravity is filtered and judged for movement by GravityTracker; the
 *          bearing is the magnetic field parser's tilted bearing of 12 o'clock.
 *          While the wrist is moving nothing is reported, so whatever was
 *          reported last stands; moving for too long, like having no gravity
 *          at all, is reported as no bearing.
 *
 *          Whether a bearing is worth showing at the tilt the face is held at
 *          is not decided here: every heading carries that tilt for whoever
 *          shows it to decide.
 *
 *          Both streams are delivered on the sensor layer's thread, and so is
 *          every report; nothing else touches the state they share.
 */
class Compass : public SDK::Interface::ISensorDataListener
{
public:
    struct Heading {
        float degrees;      ///< [0, 360) clockwise from magnetic north; 0 when not valid.
        bool  valid;        ///< Whether there is a bearing at all.
        float faceTiltDeg;  ///< 0 face up .. 180 face down; 0 when not valid.
    };

    class Listener
    {
    public:
        /// On the sensor layer's delivery thread. Must not wait.
        virtual void onCompassHeading(const Heading& heading) = 0;

    protected:
        virtual ~Listener() = default;
    };

    explicit Compass(Listener& listener);

    /**
     * @brief   Start both subscriptions.
     * @retval  'false' if there is no field stream, in which case nothing will
     *          be reported. No accelerometer stream is not a failure here:
     *          every bearing is then reported as none.
     */
    bool start();

    /**
     * @brief   End both subscriptions. Safe when not started.
     */
    void stop();

private:
    /// A bearing for every field sample at a lively rate for a screen.
    static constexpr uint16_t kFieldPeriodMs = 200;

    /// Several readings to each bearing for the tracker's filter, delivered
    /// well inside the age it still gives gravity out at.
    static constexpr uint16_t kAccelPeriodMs  = 40;
    static constexpr uint32_t kAccelLatencyMs = 100;

    void onSdlNewData(uint16_t handle, const SDK::Sensor::Data* data,
                      uint16_t count, uint16_t stride) override;

    void onAccel(const SDK::Sensor::Data* data, uint16_t count, uint16_t stride);
    void onField(const SDK::Sensor::Data* data, uint16_t count, uint16_t stride);

    Listener&          mListener;
    Sensor::Connection mFieldFeed;
    Sensor::Connection mAccelFeed;

    /// Never reset: when started again, what it holds is too old to be given
    /// out, and its first reading starts it afresh.
    GravityTracker     mGravity;
};

#endif // COMPONENT_COMPASS_HPP
