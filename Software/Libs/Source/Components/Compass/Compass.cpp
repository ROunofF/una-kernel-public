/**
 ******************************************************************************
 * @file    Compass.cpp
 * @brief   A live bearing of 12 o'clock, from the field and the accelerometer.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "Compass/Compass.hpp"

#define LOG_MODULE_PRX      "Compass"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Compass/FaceTilt.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserAccelerometer.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMagneticField.hpp"

Compass::Compass(Listener& listener)
    : mListener(listener)
    , mFieldFeed(SDK::Sensor::Type::MAGNETIC_FIELD, this, kFieldPeriodMs)
    , mAccelFeed(SDK::Sensor::Type::ACCELEROMETER, this, kAccelPeriodMs,
                 kAccelLatencyMs)
    , mGravity()
{
}

bool Compass::start()
{
    // Gravity first, so the first field sample finds some.
    if (!mAccelFeed.connect()) {
        LOG_WARNING("No accelerometer stream\n");
    }

    if (!mFieldFeed.connect()) {
        LOG_WARNING("No magnetic field stream\n");
        mAccelFeed.disconnect();
        return false;
    }

    return true;
}

void Compass::stop()
{
    mFieldFeed.disconnect();
    mAccelFeed.disconnect();
}

void Compass::onSdlNewData(uint16_t handle, const SDK::Sensor::Data* data,
                           uint16_t count, uint16_t stride)
{
    if ((data == nullptr) || (count == 0U)) {
        return;
    }

    if (mAccelFeed.matchesDriver(handle)) {
        onAccel(data, count, stride);
    } else if (mFieldFeed.matchesDriver(handle)) {
        onField(data, count, stride);
    }
}

void Compass::onAccel(const SDK::Sensor::Data* data, uint16_t count,
                      uint16_t stride)
{
    SDK::Sensor::DataBatch batch(data, count, stride);

    // Every reading, not the freshest: the tracker filters them and judges
    // each one for movement.
    for (uint16_t idx = 0; idx < batch.size(); ++idx) {
        const SDK::SensorDataParser::Accelerometer accel(batch[idx]);

        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        if (accel.getXYZ(x, y, z)) {
            mGravity.push(accel.getTimestampUs(), x, y, z);
        }
    }
}

void Compass::onField(const SDK::Sensor::Data* data, uint16_t count,
                      uint16_t stride)
{
    SDK::Sensor::DataBatch batch(data, count, stride);

    // The freshest of the batch: a bearing is a live reading.
    const SDK::SensorDataParser::MagneticField field(
            batch[static_cast<uint16_t>(batch.size() - 1)]);

    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;

    const GravityTracker::State state =
            mGravity.query(field.getTimestampUs(), gx, gy, gz);

    if (state == GravityTracker::State::MOVING) {
        // An accelerometer on a moving wrist does not read gravity. The last
        // report stands until the wrist settles, or until it has moved for
        // long enough to be reported as no bearing.
        return;
    }

    // With no gravity there is no bearing: the watch's own plane gives one
    // that turns round once enough of the vertical field is tilted into it.
    Heading heading{0.0f, false, 0.0f};

    float degrees = 0.0f;

    if ((state == GravityTracker::State::STEADY)
            && field.getAzimuthDegTilted(gx, gy, gz, degrees)) {
        heading.degrees     = degrees;
        heading.valid       = true;
        heading.faceTiltDeg = FaceTilt::degrees(gx, gy, gz);
    }

    mListener.onCompassHeading(heading);
}
