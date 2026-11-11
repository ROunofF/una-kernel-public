
#ifndef __APP_SENSOR_LISTENER_HPP
#define __APP_SENSOR_LISTENER_HPP

#include <cstdint>
#include <cstddef>
#include <memory>

#include "App/DualAppComm.hpp"
#include "App/LogThrottle.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"

namespace App
{

class SensorListener : public SDK::Interface::ISensorDataListener
{
public:
    SensorListener(App::DualAppComm& comm, bool isService);
    ~SensorListener() override = default;

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override;

private:
    bool sendMessage(SDK::MessageBase* msg);

    App::DualAppComm& mComm;
    const bool        mIsService;
    const uint32_t    mMaxPoolSize;

    /// Per-sample delivery failures, throttled. These fire at the
    /// sensor's rate for as long as the app is not draining, so unthrottled
    /// they were what saturated the log UART and reset the watch.
    LogThrottle mAllocFailLog;
    LogThrottle mSendFailLog;
};

} // namespace App

#endif // __APP_SENSOR_LISTENER_HPP
