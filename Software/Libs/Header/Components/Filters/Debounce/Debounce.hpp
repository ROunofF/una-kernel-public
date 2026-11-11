/**
 ******************************************************************************
 * @file    Debounce.hpp
 * @brief   Debounce filter class.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __FILTER_DEBOUNCE_HPP
#define __FILTER_DEBOUNCE_HPP

#include <cstdint>
#include <cstdbool>

namespace Filter
{

/**
 * @brief   %Debounce filter.
 */
class Debounce
{

public:

    /**
     * @brief   Constructor
     */
    Debounce();

    /**
     * @brief   Constructor
     */
    Debounce(uint32_t timeOn, uint32_t timeOff, bool outState);

    /**
     * @brief   Destructor
     */
    virtual ~Debounce();

    /**
     * @brief   Configure filtering time.
     * @param   timeOn: the time (ms) during which the filter input should be
     *          in the active state.
     * @param   timeOff: the time (ms) during which the filter input should be
     *          in the inactive state.
     */
    virtual void configTimings(uint32_t timeOn, uint32_t timeOff);

    /**
     * @brief   Preset the output value of the filter.
     * @param   outValue: target filter output value.
     */
    virtual void setOut(bool outState);

    /**
     * @brief   Add new input state.
     * @param   value: new value of the input.
     */
    virtual void add(bool value);

    /**
     * @brief   Get filter output value.
     * retval   filter output value.
     */
    virtual bool get();

    /**
     * @brief   Interface for class that can receive an event when the filter
     *          output changes.
     */
    class Handler
    {

    public:

        /**
         * @brief   A callback that is called when the filter output changes.
         * @param   pDebounce: pointer to the Debounce instance.
         * @param   outValue: filter output value.
         */
        virtual void outHandler(Filter::Debounce *pDebounce, bool outValue) = 0;
    protected:
        virtual ~Handler() = default;
    };

    /**
     * @brief   Set filter output handler.
     * @param   pHandler: external interrupt handler Debounce::Handler.
     */
    virtual void setOutHandler(Debounce::Handler *pHandler)
    {
        mpOutHandler = pHandler;
    }

    /**
     * @brief   Execute filter
     * @retval  'true' filter is sill active, 'false' filter is in stable state
     *          and inactive
     */
    virtual bool execute();

protected:

    virtual void onOutChange(bool out);

private:

    // The time during which the filter input should be in the active state
    uint32_t mTimeOn = 0;

    // The time during which the filter input should be in the inactive state
    uint32_t mTimeOff = 0;

    // The time stamp when you need to check the input
    uint32_t mTimeToUpd = 0;

    // Input state
    bool mInput = false;

    // Filtered output
    bool mOutput = false;

    // A flag showing that the output value has not stabilized.
    bool mIsActive = false;

    // Handler to notify that out has been changed
    Debounce::Handler *mpOutHandler = nullptr;

};



} /* namespace Filter */

#endif /* __FILTER_DEBOUNCE_HPP */
