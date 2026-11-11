/**
 ******************************************************************************
 * @file    FsmInterfaces.hpp
 * @brief   Decoupling interfaces between the Activity FSM and the data
 *          / sensor layers.
 *
 *          IFeatureSource - read-only feature queries the FSM consumes.
 *          ISensorControl - sensor (de)activation actions the FSM issues.
 *          @ref FsmSensor lives in Types.hpp.
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_FSM_INTERFACES_HPP
#define COMPONENT_ACTIVITY_FSM_INTERFACES_HPP

#include <cstdint>

#include "Activity/Types.hpp"

namespace Activity
{

/**
 * @brief Read-only feature queries consumed by the Activity FSM.
 *
 * The implementer is free to take its own short locks; the FSM makes
 * no assumptions about synchronization beyond "queries are atomic
 * relative to the data ingestion pipeline".
 */
class IFeatureSource
{
public:
    virtual ~IFeatureSource() = default;

    virtual uint32_t nowMs() const = 0;

    virtual uint32_t stepsLastMinute() const = 0;
    virtual bool     isHrValidLastMinute() const = 0;

    virtual bool     isActuallyTouched() const = 0;
    virtual bool     isMotion(uint32_t sec) const = 0;
    virtual bool     isNoMotion(uint32_t sec) const = 0;

    virtual bool     touchRaw() const = 0;
    virtual bool     motionRaw() const = 0;
};

/**
 * @brief Sensor (de)activation issued by the Activity FSM.
 *
 * Implementations must treat connect/disconnect as idempotent - the FSM
 * may call connect() on an already-connected sensor on every state-init
 * pass.
 */
class ISensorControl
{
public:
    virtual ~ISensorControl() = default;

    virtual void connect(FsmSensor s)    = 0;
    virtual void disconnect(FsmSensor s) = 0;
    virtual bool isConnected(FsmSensor s) = 0;
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_FSM_INTERFACES_HPP
