/**
 ******************************************************************************
 * @file    AccelLogger.hpp
 * @brief   Redirect data from accelerometer to the CSV file
 *
 ******************************************************************************
 */

#ifndef COMPONENT_LOGGING_ACCEL_LOGGER_HPP
#define COMPONENT_LOGGING_ACCEL_LOGGER_HPP

#include "Pmic.hpp"
#include "FuelGauge.hpp"
#include "Loggers/CSVLogger.hpp"

#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"

#include <string>

namespace Component {

class AccelLogger : public SDK::Interface::ISensorDataListener
{
public:
    AccelLogger();

    void start();
    void stop();

private:
    static constexpr char mHeader[]     = "ACCX,ACCY,ACCZ,GYROX,GYROY,GYROZ,WRIST";
    static constexpr char mFolderName[] = "2:/AccelLog/";
    // Session files to keep; oldest pruned by NNNN on each start(). Tunable.
    static constexpr uint32_t skKeepNewest = 10;

    CSVLogger   mLogger;

    Sensor::Connection mDriverFusion;
    Sensor::Connection mDriverAccel;
    Sensor::Connection mDriverGyro;
    Sensor::Connection mDriverWrist;


    uint32_t mStartTime;
    uint32_t mWristEvent;
    uint32_t mWristTs;


    void onSdlNewData(uint16_t                 handle,
                      const SDK::Sensor::Data* base,
                      uint16_t                 count,
                      uint16_t                 stride) override;

};

} // namespace Component

#endif // COMPONENT_LOGGING_ACCEL_LOGGER_HPP
