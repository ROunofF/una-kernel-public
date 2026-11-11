/**
 ******************************************************************************
 * @file    IFsmTrigger.hpp
 * @brief   Out-of-cadence wake-up signal for the activity FSM.
 *
 *          The activity FSM normally ticks on a fixed timer (~30 s).
 *          That cadence is fine for everything except sensor edges
 *          where the user expects an immediate response - putting the
 *          watch on, taking it off, the first burst of motion. For
 *          those events the data layer asks for an extra tick through
 *          IFsmTrigger; the cadence timer keeps running unchanged.
 *
 *          Implementations must accept calls from any task, including
 *          the sensor-layer task and from inside short critical
 *          sections - Manager simply pushes a Cmd on its queue, which
 *          is safe in either context.
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_IFSM_TRIGGER_HPP
#define COMPONENT_ACTIVITY_IFSM_TRIGGER_HPP

namespace Activity
{

class IFsmTrigger
{
public:
    virtual ~IFsmTrigger() = default;

    /**
     * @brief Request an out-of-cadence FSM tick. Idempotent: redundant
     *        calls before the FSM has run will collapse into a single
     *        tick (the queue carries a single FSM_TICK at a time).
     */
    virtual void requestFsmTick() = 0;
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_IFSM_TRIGGER_HPP
