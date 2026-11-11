/**
 ******************************************************************************
 * @file    SensorDataSample.hpp
 * @brief   Sensor data sample class with abbility to extend the cells count
 *          and access to the data fields.
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __SENSOR_DATA_SAMPLE_HPP
#define __SENSOR_DATA_SAMPLE_HPP

#include "SDK/SensorLayer/SensorData.hpp"

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstring>

namespace Sensor
{

struct DataSample
{
    struct FloatWriter {
        SDK::Sensor::Data& data;
        uint16_t           fieldCount;

        float& operator[](uint16_t idx) const noexcept;
    };

    struct U32Writer {
        SDK::Sensor::Data& data;
        uint16_t           fieldCount;

        uint32_t& operator[](uint16_t idx) const noexcept;
    };

    struct I32Writer {
        SDK::Sensor::Data& data;
        uint16_t           fieldCount;

        int32_t& operator[](uint16_t idx) const noexcept;
    };

    explicit DataSample(uint16_t fieldCount);

    ~DataSample();

    DataSample(const DataSample&)            = delete;
    DataSample& operator=(const DataSample&) = delete;

    DataSample(DataSample&& other)            = delete;
    DataSample& operator=(DataSample&& other) = delete;

    SDK::Sensor::Data& getData();

    const SDK::Sensor::Data& getData() const;

    std::size_t getFieldCount() const;

    void setTimestamp(uint32_t ms) const;
    void setTimestampUs(uint64_t usTotal) const;

private:
    SDK::Sensor::Data* mData;
    std::size_t        mFieldCount;

public:
    // sub-writers
    FloatWriter f;
    U32Writer   u;
    I32Writer   i;

private:
    static SDK::Sensor::Data* createData(std::size_t fieldCount);

    /// createData() for the member initialiser list, with the null check
    /// applied INSIDE it. The writers below bind references from *mData, and
    /// a member initialiser list runs in declaration order, so mData is
    /// produced before they bind - which is the only place the check can sit
    /// and still run first. See the definition for why that ordering matters
    /// rather than being tidiness.
    static SDK::Sensor::Data* createDataChecked(std::size_t fieldCount);
};

} /* namespace SDK::Sensor */

#endif /* __SENSOR_DATA_SAMPLE_HPP */
