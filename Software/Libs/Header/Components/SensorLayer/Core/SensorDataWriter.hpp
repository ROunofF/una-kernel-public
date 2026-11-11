/**
 ******************************************************************************
 * @file    SensorData.hpp
 * @brief   Sensor data writer class
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __SENSOR_DATA_WRITER_HPP
#define __SENSOR_DATA_WRITER_HPP

#include "SDK/SensorLayer/SensorData.hpp"
#include <cstdint>

namespace Sensor
{

struct DataWriter
{
    struct FloatWriter {
        SDK::Sensor::Data& data;
        uint16_t           fieldCount;

        float& operator[](uint16_t idx) noexcept
        {
            assert(idx < fieldCount);
            return data.mValue[idx].f;
        }
    };

    struct U32Writer {
        SDK::Sensor::Data& data;
        uint16_t           fieldCount;

        uint32_t& operator[](uint16_t idx) noexcept
        {
            assert(idx < fieldCount);
            return data.mValue[idx].u32;
        }
    };

    struct I32Writer {
        SDK::Sensor::Data& data;
        uint16_t           fieldCount;

        int32_t& operator[](uint16_t idx) noexcept
        {
            assert(idx < fieldCount);
            return data.mValue[idx].i32;
        }
    };

    DataWriter(SDK::Sensor::Data& data, uint16_t fieldCount)
        : f{data, fieldCount}
        , u{data, fieldCount}
        , i{data, fieldCount}
        , mData(data)
        , mFieldCount(fieldCount)
    {}

    void setTimestamp(uint32_t ms) const noexcept
    {
        mData.mTimeStamp = ms;
    }

    void setTimestampUs(uint64_t usTotal) const noexcept
    {
        // mTimeStamp — ms, mTimeStampUs — "a tail" in us (0..999)
        mData.mTimeStamp   = static_cast<uint32_t>(usTotal / 1000ULL);
        mData.mTimeStampUs = static_cast<uint32_t>(usTotal % 1000ULL); // 0..999
    }

    uint16_t getFieldCount() const noexcept
    {
        return mFieldCount;
    }

    // sub-writers
    FloatWriter f;
    U32Writer   u;
    I32Writer   i;

private:
    SDK::Sensor::Data& mData;
    const uint16_t     mFieldCount;
};

} /* namespace SDK::Sensor */

#endif /* __SENSOR_DATA_WRITER_HPP */
