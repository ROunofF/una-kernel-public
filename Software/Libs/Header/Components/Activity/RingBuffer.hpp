/**
 ******************************************************************************
 * @file    RingBuffer.hpp
 * @brief   Fixed-capacity ring buffer used by the feature trackers.
 *
 *          Header-only template. push_back overwrites the oldest entry
 *          when full, pop_front advances the head; both are constant
 *          time. Index 0 = oldest live entry.
 ******************************************************************************
 */

#ifndef COMPONENT_ACTIVITY_RING_BUFFER_HPP
#define COMPONENT_ACTIVITY_RING_BUFFER_HPP

#include <array>
#include <cstddef>

namespace Activity
{

template <typename T, size_t Capacity>
class RingBuffer
{
public:
    constexpr RingBuffer() = default;

    bool empty() const { return mSize == 0; }
    bool full()  const { return mSize == Capacity; }
    size_t size() const { return mSize; }
    constexpr size_t capacity() const { return Capacity; }

    void clear()
    {
        mHead = 0;
        mSize = 0;
    }

    T&       front()       { return mBuf[mHead]; }
    const T& front() const { return mBuf[mHead]; }

    T&       back()       { return mBuf[(mHead + mSize - 1) % Capacity]; }
    const T& back() const { return mBuf[(mHead + mSize - 1) % Capacity]; }

    /// Push to back, overwrite oldest if full.
    void push_back(const T& value)
    {
        if (mSize < Capacity) {
            mBuf[(mHead + mSize) % Capacity] = value;
            ++mSize;
        } else {
            mBuf[mHead] = value;
            mHead = (mHead + 1) % Capacity;
        }
    }

    /// Pop from front. Caller must ensure !empty().
    void pop_front()
    {
        if (mSize > 0) {
            mHead = (mHead + 1) % Capacity;
            --mSize;
        }
    }

    /// Index access: 0 = oldest live entry.
    T&       operator[](size_t index)       { return mBuf[(mHead + index) % Capacity]; }
    const T& operator[](size_t index) const { return mBuf[(mHead + index) % Capacity]; }

private:
    std::array<T, Capacity> mBuf{};
    size_t                  mHead = 0;
    size_t                  mSize = 0;
};

} // namespace Activity

#endif // COMPONENT_ACTIVITY_RING_BUFFER_HPP
