/**
 ******************************************************************************
 * @file    PushFileWriter.hpp
 * @brief   Async file writer fed by pushing bytes into a stream buffer.
 ******************************************************************************
 */

#ifndef SINK_PUSH_FILE_WRITER_HPP
#define SINK_PUSH_FILE_WRITER_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "OS.hpp"
#include "Sink/AsyncFileWriter.hpp"

namespace Sink {

/**
 * @brief AsyncFileWriter you feed: write() queues bytes into a stream buffer the
 *        worker drains. The classic logger transport.
 */
class PushFileWriter : public AsyncFileWriter {
public:
    PushFileWriter();
    ~PushFileWriter() override;

    bool write(const void* data, size_t size);
    bool write(const char* str);
    bool writef(const char* format, ...) __attribute__((format(printf, 2, 3)));
    bool write(int32_t value);
    bool write(uint32_t value);
    bool write(float value, uint8_t precision = 2);
    bool write(std::string_view sv);

    /**
     * @brief Cap how long write() may block when the stream buffer is full.
     *
     * Default is osWaitForever (back-pressure). Writers fed from sensor data
     * callbacks MUST set 0: those run inside the sensor manager's refresh
     * sweep, and blocking there stalls the whole sensor layer behind a
     * wedged file backend — dropping the record is the correct trade.
     */
    void setWriteTimeout(uint32_t timeoutMs) { mWriteTimeoutMs = timeoutMs; }

protected:
    size_t produce(uint8_t* dst, size_t maxLen) override;
    bool   sourceHasData() override { return mStreamBuffer.getCount() > 0; }

private:
    OS::StreamBuffer<1024> mStreamBuffer;
    uint32_t               mWriteTimeoutMs { osWaitForever };
};

}  // namespace Sink

#endif  // SINK_PUSH_FILE_WRITER_HPP
