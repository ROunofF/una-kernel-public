/**
 ******************************************************************************
 * @file    AccelBleLogger.hpp
 * @brief   Redirect data from accelerometer to the BLE NUS Tx
 *
 ******************************************************************************
 */

#ifndef COMPONENT_LOGGERS_ACCEL_BLE_LOGGER_HPP
#define COMPONENT_LOGGERS_ACCEL_BLE_LOGGER_HPP

#include "BLE/Services/NordicUartService.hpp"

#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"

#include <string>

namespace Component {

class AccelBleLogger : public SDK::Interface::ISensorDataListener
{
public:
    AccelBleLogger();
    virtual ~AccelBleLogger() = default;
    void init(Ble::NordicUartService &srv) { mSrv = &srv; }

    void start();
    void stop();

private:

    Ble::NordicUartService *mSrv = nullptr;

    Sensor::Connection mDriverFusion;
    Sensor::Connection mDriverWrist;

    bool mIsStarted;
    uint32_t mStartTime;
    uint32_t mWristEvent;
    uint32_t mWristTs;
    char mString[128];

    static constexpr char skSampleRateHz = 100.0f;

    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override;

};

} // namespace Component

#endif // COMPONENT_LOGGING_ACCEL_LOGGER_BLE_HPP
