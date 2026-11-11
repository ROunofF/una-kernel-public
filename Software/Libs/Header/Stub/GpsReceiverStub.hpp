/**
 ******************************************************************************
 * @file    GpsReceiverStub.hpp
 * @brief   Stub for GPS component. This is used when the system cannot find or
 *          recognize the actual GPS module.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __STUB_GPS_RECEIVER_HPP
#define __STUB_GPS_RECEIVER_HPP

#include "IGps.hpp"

namespace Stub
{

class GpsReceiverStub : public Interface::IGps {

public:

    GpsReceiverStub() = default;

    virtual ~GpsReceiverStub() = default;

    virtual bool init() override { return false; }

    virtual bool deinit() override { return false; }

    virtual bool enable() override { return false; }

    virtual bool disable() override { return false; }

    virtual bool isEnabled() override { return false; }

    virtual void attachCallback(Interface::IGps::Callback *pCallback) override {}

    virtual void detachCallback(Interface::IGps::Callback *pCallback) override {}

    virtual void setPeriod(uint32_t seconds) override {}

    virtual bool hasFix() override { return false; }

    virtual time_t getTime() override { return {}; }

    virtual float getSpeed() override { return 0.0f; }

    virtual float getDistance() override { return 0.0f; }

    virtual Interface::IGps::LocationInfo getLocation() override { return {}; }

protected:

private:

};

} /* namespace Stub */

#endif /* __STUB_GPS_RECEIVER_HPP */
