/**
 ******************************************************************************
 * @file    FifoFrames.hpp
 * @brief   The frames of a BMI270 FIFO read in header mode.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef HARDWARE_BMI270_FIFO_FRAMES_HPP
#define HARDWARE_BMI270_FIFO_FRAMES_HPP

#include <cstdint>

namespace Hardware::Bmi270Fifo
{

/// What a frame is. The first three carry readings; the rest are control
/// frames, which the part puts among them.
enum class FrameType : uint8_t {
    Accel,          ///< One accelerometer reading
    Gyro,           ///< One gyroscope reading
    GyroAccel,      ///< One of each, taken at the same instant: gyroscope first
    Skip,           ///< How many frames the part dropped when the FIFO overflowed
    SensorTime,     ///< The part's counter as the last frame before it was read out
    InputConfig     ///< A change of the sensors' settings that takes effect with the next frame
};

/// One frame of a read.
struct Frame {
    FrameType      type    = FrameType::Accel;
    const uint8_t* payload = nullptr;   ///< The bytes after the header
};

/// Why a walk over a read came to its end.
enum class ReadEnd : uint8_t {
    Data,       ///< The data ran out on a frame boundary
    Partial,    ///< The data ran out inside a frame; the part gives that frame again, whole
    Filler,     ///< The part had no more frames and filled the rest of the read
    Unknown     ///< A header that is none of the frames known here
};

/// Bits of an InputConfig frame's first byte: the registers whose new values
/// take effect. None set: the FIFO or a sensor was switched on.
constexpr uint8_t kAccelConfChanged  = 0x01;
constexpr uint8_t kAccelRangeChanged = 0x02;
constexpr uint8_t kGyroConfChanged   = 0x04;
constexpr uint8_t kGyroRangeChanged  = 0x08;

/// Bytes of one reading, and of the longest frame with its header.
constexpr uint16_t kReadingSize  = 6;
constexpr uint16_t kMaxFrameSize = 1 + 2 * kReadingSize;

/// Bytes of a SensorTime frame with its header: what a read has to ask for
/// past the last reading for the part to send one.
constexpr uint16_t kSensorTimeFrameSize = 4;

/// A 24-bit counter value, least significant byte first.
inline uint32_t counter24(const uint8_t* bytes)
{
    return static_cast<uint32_t>(bytes[0])
         | (static_cast<uint32_t>(bytes[1]) << 8)
         | (static_cast<uint32_t>(bytes[2]) << 16);
}

/// The frames a Skip frame says were dropped; 255 stands for that many or more.
inline uint8_t skippedFrames(const Frame& frame)
{
    return frame.payload[0];
}

/// The counter value of a SensorTime frame.
inline uint32_t sensorTime(const Frame& frame)
{
    return counter24(frame.payload);
}

/// What an InputConfig frame says changed: the k...Changed bits.
inline uint8_t configChanges(const Frame& frame)
{
    return frame.payload[0];
}

/// The counter value an InputConfig frame gives for the frame after it.
inline uint32_t configSensorTime(const Frame& frame)
{
    return counter24(frame.payload + 1);
}

/**
 * @brief Walks the frames of a FIFO read, one at a time, in the order the
 *        part stored them.
 *
 * A read seldom ends where the FIFO does. It may stop inside a frame, and
 * the part then gives that frame again from its header with the next read;
 * or it may run past the last frame, and the part fills the rest. The walk
 * stops at either, and offset() is where the next read's bytes belong: right
 * after the last whole frame.
 *
 * Frames with auxiliary sensor data are not known here: their length depends
 * on how the auxiliary interface is set up.
 */
class FrameReader
{
public:
    /**
     * @param data   The bytes read from FIFO_DATA.
     * @param length How many there are.
     * @param offset Where to start: 0, or offset() of a walk over fewer bytes
     *               of the same read.
     */
    FrameReader(const uint8_t* data, uint16_t length, uint16_t offset = 0)
        : mData(data)
        , mLength(length)
        , mOffset(offset)
    {
    }

    /**
     * @brief Take the next frame.
     * @param frame Filled in when there is one.
     * @return false when the walk has come to its end; end() says why.
     */
    bool next(Frame& frame)
    {
        if (mOffset >= mLength) {
            mEnd = ReadEnd::Data;
            return false;
        }

        // The two low bits of a header are interrupt tags, not the frame's kind.
        const uint8_t header  = mData[mOffset] & 0xFCu;
        uint16_t      payload = 0;

        switch (header) {
            case kHeaderAccel:
                frame.type = FrameType::Accel;
                payload    = kReadingSize;
                break;

            case kHeaderGyro:
                frame.type = FrameType::Gyro;
                payload    = kReadingSize;
                break;

            case kHeaderGyroAccel:
                frame.type = FrameType::GyroAccel;
                payload    = 2 * kReadingSize;
                break;

            case kHeaderSkip:
                frame.type = FrameType::Skip;
                payload    = 1;
                break;

            case kHeaderSensorTime:
                frame.type = FrameType::SensorTime;
                payload    = 3;
                break;

            case kHeaderInputConfig:
                frame.type = FrameType::InputConfig;
                payload    = 4;
                break;

            case kHeaderFiller:
                mEnd = ReadEnd::Filler;
                return false;

            default:
                mEnd = ReadEnd::Unknown;
                return false;
        }

        if (mOffset + 1u + payload > mLength) {
            mEnd = ReadEnd::Partial;
            return false;
        }

        frame.payload = mData + mOffset + 1;
        mOffset       = static_cast<uint16_t>(mOffset + 1u + payload);

        return true;
    }

    /// Why next() returned false.
    ReadEnd end() const
    {
        return mEnd;
    }

    /// The end of the last whole frame taken.
    uint16_t offset() const
    {
        return mOffset;
    }

private:
    static constexpr uint8_t kHeaderSkip        = 0x40;
    static constexpr uint8_t kHeaderSensorTime  = 0x44;
    static constexpr uint8_t kHeaderInputConfig = 0x48;
    static constexpr uint8_t kHeaderFiller      = 0x80;
    static constexpr uint8_t kHeaderAccel       = 0x84;
    static constexpr uint8_t kHeaderGyro        = 0x88;
    static constexpr uint8_t kHeaderGyroAccel   = 0x8C;

    const uint8_t* mData;
    uint16_t       mLength;
    uint16_t       mOffset;
    ReadEnd        mEnd = ReadEnd::Data;
};

/**
 * @brief How many bytes to ask the part for, to reach the end of a FIFO
 *        that held fifoLength bytes a moment ago.
 *
 * The part sends its SensorTime frame only to a read that empties the FIFO,
 * and the FIFO goes on filling while it is read, the faster the data rates
 * the more. So the read asks for three tenths more than the FIFO held, and
 * for the SensorTime frame with a frame or so to spare. A read that still
 * falls short is followed by another.
 *
 * @param fifoLength What FIFO_LENGTH said.
 * @param room       The most the read may be.
 * @return Bytes to read, no more than room.
 */
inline uint16_t readLength(uint16_t fifoLength, uint16_t room)
{
    const uint32_t wanted = fifoLength + (fifoLength * 3u) / 10u + kSensorTimeFrameSize + kMaxFrameSize - 1u;

    return (wanted < room) ? static_cast<uint16_t>(wanted) : room;
}

} // namespace Hardware::Bmi270Fifo

#endif // HARDWARE_BMI270_FIFO_FRAMES_HPP
