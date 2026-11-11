/**
 ******************************************************************************
 * @file    IBarometer.hpp
 * @brief   Barometer (pressure and temperature) interface.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef INTERFACE_IBAROMETER_HPP
#define INTERFACE_IBAROMETER_HPP

#include <cstdint>

namespace Interface
{

/**
 * @brief   Barometer (pressure and temperature) interface.
 *
 * The API splits into levels with different constraints:
 *   - Lifecycle:         blocking, single-threaded, caller-ordered setup/teardown.
 *   - Streaming control: asynchronous requests, safe to call from any thread.
 * Samples are pushed (DataCallback) as the part measured them.
 *
 * Rates are in Hz. The rates on offer form a ladder in which every period is a
 * whole multiple of every shorter one. A request means "at least this fast"
 * and gets the slowest rung that meets it, so the applied rate is not reported
 * back.
 */
class IBarometer
{

public:

    /**
     * @brief   New-sample callback. One measurement delivers one pressure and
     *          one temperature, both carrying the same instant.
     * @note    Called on a thread the implementation chooses. A measurement
     *          that fails delivers nothing rather than repeating the previous
     *          one.
     */
    class DataCallback
    {
    public:
        /**
         * @brief   Called with a new pressure sample.
         * @param   timestampUs: When the measurement was started, microseconds,
         *          on the same system time base the other sensors use.
         *          Microseconds are the unit, not the resolution. It is read
         *          before the bus is taken, so it comes ahead of the
         *          conversion by however long another device held the bus.
         * @param   mbar: Pressure, millibar.
         * @param   periodUs: The period the part was measuring at for this
         *          sample, as it was set, not as measured.
         */
        virtual void onPressure(uint64_t timestampUs, float mbar,
                                uint32_t periodUs)
        {
            (void) timestampUs;
            (void) mbar;
            (void) periodUs;
        }

        /**
         * @brief   Called with a new temperature sample.
         * @param   timestampUs: When the measurement was started, microseconds,
         *          on the same system time base the other sensors use.
         *          Microseconds are the unit, not the resolution.
         * @param   degC: Temperature, degrees Celsius.
         * @param   periodUs: The period the part was measuring at for this
         *          sample, as it was set, not as measured.
         */
        virtual void onTemperature(uint64_t timestampUs, float degC,
                                   uint32_t periodUs)
        {
            (void) timestampUs;
            (void) degC;
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
     * @brief   Set the sample callback. One slot.
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
     * @note    On a running stream only the rate changes: the part is not
     *          restarted. The request is applied as given; reconciling several
     *          callers' rates is not done here.
     * @note    A stream the implementation stopped trying to recover is
     *          started afresh, so asking again is how a caller retries.
     * @param   sampleRateHz: Requested rate, Hz. Satisfied by a rung at least
     *          this fast, or by the fastest on offer if none is. A value below
     *          the slowest rung, or NaN, gets the slowest rung.
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
     * @brief   Fastest rate on offer, Hz. A request above it is satisfied by
     *          this rate rather than refused.
     */
    virtual float getMaxSampleRateHz() const = 0;

    /**
     * @brief   Slowest rate on offer, Hz. A request below it is satisfied by
     *          this rate rather than refused.
     */
    virtual float getMinSampleRateHz() const = 0;

protected:

    virtual ~IBarometer() = default;

};

} // namespace Interface

#endif // INTERFACE_IBAROMETER_HPP
