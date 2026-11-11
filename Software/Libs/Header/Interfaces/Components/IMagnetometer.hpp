/**
 ******************************************************************************
 * @file    IMagnetometer.hpp
 * @brief   Magnetometer (3-axis magnetic field) interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INTERFACE_IMAGNETOMETER_HPP
#define INTERFACE_IMAGNETOMETER_HPP

#include <cstdint>

namespace Interface
{

/**
 * @brief   Magnetometer (3-axis magnetic field) interface.
 *
 * The API splits into levels with different constraints:
 *   - Lifecycle:         blocking, single-threaded, caller-ordered setup/teardown.
 *   - Streaming control: asynchronous requests, safe to call from any thread.
 * Samples are pushed (DataCallback) as the part measured them, uncorrected.
 *
 * Rates are in Hz, floating point because a part's rate ladder need not be
 * whole numbers. A request means "at least this fast" and gets the slowest
 * rung that meets it, so the applied rate is not reported back.
 */
class IMagnetometer
{

public:

    /**
     * @brief   New-sample callback, invoked once per measurement.
     * @note    Called on a thread the implementation chooses. A read that fails
     *          delivers nothing rather than repeating the previous sample.
     */
    class DataCallback
    {
    public:
        /**
         * @brief   Called with a new magnetic field sample.
         * @param   timestampUs: Acquisition time, microseconds, on the same
         *          system time base the other sensors use. Microseconds are the
         *          unit, not the resolution: do not assume millisecond
         *          boundaries.
         * @param   x: Magnetic flux density along X, uT.
         * @param   y: Magnetic flux density along Y, uT.
         * @param   z: Magnetic flux density along Z, uT.
         * @param   periodUs: The period the part was converting at for this
         *          sample, as it was set, not as measured.
         */
        virtual void onMagnetic(uint64_t timestampUs, float x, float y, float z,
                                uint32_t periodUs)
        {
            (void) timestampUs;
            (void) x;
            (void) y;
            (void) z;
            (void) periodUs;
        }

    protected:

        virtual ~DataCallback() = default;
    };

    /* ===== Lifecycle (blocking, single-threaded, caller-ordered) ===== */

    /**
     * @brief   Probe the part and bring it up, leaving it powered down.
     * @retval  'true' if a supported part is present, 'false' otherwise.
     */
    virtual bool init() = 0;

    /**
     * @brief   Stop sampling and put the part back into its lowest-power state.
     * @note    Do NOT call while holding a lock that the DataCallback path
     *          also takes.
     */
    virtual void deinit() = 0;

    /**
     * @brief   Check whether init() has succeeded.
     */
    virtual bool isInit() const = 0;

    /**
     * @brief   Set the sample callback: the field exactly as the part measured
     *          it, in the watch's axes. One slot.
     * @note    Detaching with nullptr does not wait for a call in progress, so
     *          on its own it does not make a subscriber safe to destroy.
     *          deinit() does: nothing is delivered after it returns until the
     *          part is brought up again.
     * @param   pCallback: Callback to receive samples, or nullptr to detach.
     */
    virtual void setDataCallback(DataCallback* pCallback) = 0;

    /* ===== Streaming control (asynchronous requests) =====
     *
     * Both post an intent and return. 'true' means the request was taken and
     * will be acted on; 'false' means nothing will come of it. Both refuse
     * unless init() has succeeded.
     */

    /**
     * @brief   Request sampling at the given rate, or a rate change on a
     *          stream already running.
     * @note    On a running stream only the rate changes: the stream is not
     *          restarted and loses no samples. The request is applied as
     *          given; reconciling several callers' rates is not done here.
     * @note    A stream the implementation stopped trying to recover is
     *          started afresh, so asking again is how a caller retries.
     * @param   sampleRateHz: Requested rate, Hz. Satisfied by a rung at least
     *          this fast, or by the fastest the part has if none is. A value
     *          below the slowest rung, or NaN, gets the slowest rung.
     */
    virtual bool requestStart(float sampleRateHz) = 0;

    /**
     * @brief   Request that sampling stop and the part power down.
     * @note    Accepting one on a part already stopped is not an error.
     */
    virtual bool requestStop() = 0;

    /**
     * @brief   Whether the part is currently streaming.
     * @note    Reports what has taken effect, not what has been requested, and
     *          can be stale; it is not a gate for the next request. Requests
     *          are ordered against each other by the implementation.
     */
    virtual bool isRunning() const = 0;

    /**
     * @brief   Fastest rate the part can sustain, Hz. A request above it is
     *          satisfied by this rate rather than refused.
     */
    virtual float getMaxSampleRateHz() const = 0;

    /**
     * @brief   Slowest rate the part offers, Hz. A request below it is
     *          satisfied by this rate rather than refused.
     */
    virtual float getMinSampleRateHz() const = 0;

protected:

    virtual ~IMagnetometer() = default;

};

} // namespace Interface

#endif // INTERFACE_IMAGNETOMETER_HPP
