/**
 ******************************************************************************
 * @file    InstanceActivityService.hpp
 * @brief   Singleton holder for the Activity subsystem.
 *
 *          Owns FeatureCenter, SensorPolicy, and Service - the three
 *          long-lived objects the Activity FSM needs to run. An
 *          independent singleton, so the FSM / sensor pipeline can be
 *          brought up and exercised on its own.
 *
 *          Header-only: trivial glue with no out-of-line state, so
 *          there is no matching .cpp.
 ******************************************************************************
 */

#ifndef INSTANCE_ACTIVITY_SERVICE_HPP
#define INSTANCE_ACTIVITY_SERVICE_HPP

#include "Activity/FeatureCenter.hpp"
#include "Activity/SensorPolicy.hpp"
#include "Activity/Service.hpp"
#include "Activity/IActivityDataSource.hpp"

namespace Instance {

class ActivityService
{
public:
    static ActivityService& getInstance()
    {
        static ActivityService inst;
        return inst;
    }

    Activity::Service&       getService()       { return mService; }
    Activity::SensorPolicy&  getSensorPolicy()  { return mSensorPolicy; }
    Activity::FeatureCenter& getFeatureCenter() { return mFeatures; }

    /// Stable IActivityDataSource view for consumers that
    /// should not depend on the concrete SensorPolicy class.
    Activity::IActivityDataSource& getDataSource() { return mSensorPolicy; }

private:
    ActivityService()
        : mFeatures()
        , mSensorPolicy(mFeatures)
        , mService(mFeatures,       // FeatureCenter (FSM uses IFeatureSource view)
                   mSensorPolicy)   // ISensorControl
    {
        // Out-of-cadence FSM wake-ups: FeatureCenter detects touch /
        // motion edges on the sensor task and pokes Service.
        mFeatures.attachTrigger(mService);

        // SensorPolicy caches the activity-minute total so an
        // IActivityDataSource consumer gets cheap reads instead of
        // mutating the tracker. Service is the sole ticker, fires the
        // listener on each FSM tick when the value advances.
        mService.attachDurationListener(&mSensorPolicy);
    }

    ~ActivityService() = default;

    Activity::FeatureCenter mFeatures;
    Activity::SensorPolicy  mSensorPolicy;
    Activity::Service       mService;
};

} // namespace Instance

#endif // INSTANCE_ACTIVITY_SERVICE_HPP
