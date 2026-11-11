/**
 ******************************************************************************
 * @file    SensorDataSample.cpp
 * @brief   Sensor data sample class with abbility to extend the cells count
 *          and access to the data fields.
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "SensorLayer/Core/SensorDataSample.hpp"

#include <new>   // std::nothrow, std::align_val_t

namespace Sensor {

float& DataSample::FloatWriter::operator[](uint16_t idx) const noexcept
{
    assert(idx < fieldCount);
    return data.mValue[idx].f;
}

uint32_t& DataSample::U32Writer::operator[](uint16_t idx) const noexcept
{
    assert(idx < fieldCount);
    return data.mValue[idx].u32;
}

int32_t& DataSample::I32Writer::operator[](uint16_t idx) const noexcept
{
    assert(idx < fieldCount);
    return data.mValue[idx].i32;
}

DataSample::DataSample(uint16_t fieldCount)
        : mData(createDataChecked(fieldCount))
        , mFieldCount(fieldCount)
        , f{*mData, fieldCount}
        , u{*mData, fieldCount}
        , i{*mData, fieldCount}
{
    assert(fieldCount > 0);
}

SDK::Sensor::Data* DataSample::createDataChecked(std::size_t fieldCount)
{
    SDK::Sensor::Data* data = createData(fieldCount);

    // The check belongs HERE, not in the constructor body, and the reason is
    // the same mechanism that made the old `if (!mem)` dead code.
    //
    // The writers bind references from *mData in the member initialiser list.
    // If the check sat in the constructor body it would run AFTER that bind,
    // so the dereference would precede it - and a compiler is entitled to
    // conclude from a dereference that the pointer is non-null and delete the
    // later check as unreachable. Exactly what GCC does after a throwing
    // operator new. Placing it in the initialiser that produces the pointer
    // puts it before any dereference, where it cannot be reasoned away.
    //
    // Fatal rather than declined, because DataSample is a by-value member of
    // SensorDriver and construction cannot be refused from there. That is a
    // real fatality, but an attributed one: __assert_func routes to
    // CrashDump_handlerAssert, which latches file/line/func/expr, logs them
    // and captures a full dump. NDEBUG is not set in the firmware build, so
    // this is live in what ships. The alternative is a write through a null
    // reference to address 0 - the boot alias on this part, so it may be
    // swallowed rather than reported.
    assert(data != nullptr);

    return data;
}

DataSample::~DataSample()
{
    ::operator delete(mData, std::align_val_t(4));
//    delete [] reinterpret_cast<uint8_t*>(mData);
}

SDK::Sensor::Data& DataSample::getData() noexcept
{
    return *mData;
}

const SDK::Sensor::Data& DataSample::getData() const noexcept
{
    return *mData;
}

std::size_t DataSample::getFieldCount() const noexcept
{
    return mFieldCount;
}

void DataSample::setTimestamp(uint32_t ms) const noexcept
{
    mData->mTimeStamp   = ms;
    mData->mTimeStampUs = 0;
}

void DataSample::setTimestampUs(uint64_t usTotal) const noexcept
{
    // mTimeStamp — ms, mTimeStampUs — "a tail" in us (0..999)
    mData->mTimeStamp   = static_cast<uint32_t>(usTotal / 1000ULL);
    mData->mTimeStampUs = static_cast<uint32_t>(usTotal % 1000ULL); // 0..999
}

SDK::Sensor::Data* DataSample::createData(std::size_t fieldCount)
{
    assert(fieldCount > 0);

    // Data has already contain one Cell (mValues[1]),
    // so add (fieldCount - 1) extra
    std::size_t bytes = sizeof(SDK::Sensor::Data) + (fieldCount - 1u) * sizeof(SDK::Sensor::Data::Field);

    // The nothrow form, so the null check below is real code. The throwing
    // aligned operator this replaced could not return null: on failure it
    // called std::terminate -> abort(), and GCC deletes a null check after
    // the throwing form, so the guard below compiled to nothing.
    //
    // What that cost was ATTRIBUTION, not the report. abort() is the
    // kernel's own (heap_useNewlib_ST.cpp), which logs "Abort called!" and
    // then configASSERT(0) -> __assert_func -> CrashDump_handlerAssert, so
    // the failure was already logged and dumped. But the assert it raises
    // names the line inside abort(), and whether the offline unwinder gets
    // back through terminate and __cxa_throw to the allocation that actually
    // died is not something to rely on. Failing here names this line.
    //
    // The two-ended heap is why this matters now. With app images out of the kernel arena
    // the kernel is the side that runs out of room, so a kernel allocation
    // failing stopped being hypothetical.
    uint8_t* mem = (uint8_t*)::operator new(bytes, std::align_val_t(4),
                                            std::nothrow);

    if (!mem) {
        return nullptr;
    }

    std::memset(mem, 0, bytes);

    return reinterpret_cast<SDK::Sensor::Data*>(mem);
}

}
