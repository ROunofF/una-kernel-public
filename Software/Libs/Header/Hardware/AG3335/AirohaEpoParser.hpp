/**
 ******************************************************************************
 * @file    AirohaEpoParser.hpp
 * @brief   Universal parser for EPO/QEPO/ELPO/QELPO files.
 ******************************************************************************
 *
 ******************************************************************************
 */

#pragma once

#include <cstdint>
#include <cstddef>

#include "AG3335/AirohaType.hpp"

namespace Airoha
{

/**
 * @brief: EPO file format types
 */
enum class EpoFileType : uint8_t {
    EPO_STANDARD,      // GPS/GLONASS 15-day format
    EPO_VARIABLE,      // Galileo/BeiDou with header
    QEPO_FIXED,        // GPS/GLONASS 6-hour fixed
    QEPO_VARIABLE,     // Galileo/BeiDou 6-hour with header
    ELPO_STANDARD,     // GPS/GLONASS/QZSS 3-7 day
    ELPO_VARIABLE,     // Galileo/BeiDou 3-7 day with header
    UNKNOWN
};

/**
 * @brief: Single EPO segment data (72 bytes)
 */
struct Segment {
    const uint8_t* raw;     // Pointer to the 72-byte segment; valid only while it is being visited
    uint32_t gpsHour;       // GPS hour since epoch
    uint8_t svId;           // Satellite Vehicle ID
    uint8_t prn;            // Satellite PRN number (see svIdToPRN())
    uint8_t systemId;       // System ID (0=GPS, 1=GLO, 2=GAL, 3=BDS, 4=QZSS)
    uint32_t checksum;      // Segment checksum
};

/**
 * @brief: Random-access source of an EPO file's bytes.
 *
 * The parser reads through this rather than holding the file, so it never
 * needs a buffer the size of the file: a GPS capsule is 64 KB, of which an
 * injection sends one 6-hour block.
 */
class EpoReader {
public:
    /**
     * @brief: Size of the file in bytes.
     */
    virtual size_t size() const = 0;

    /**
     * @brief: Read exactly @p n bytes at @p offset.
     * @retval: false on any error or short read.
     */
    virtual bool read(size_t offset, uint8_t* dst, size_t n) = 0;

protected:
    ~EpoReader() = default;
};

/**
 * @brief: EpoReader over a file that is already in memory.
 */
class MemoryEpoReader final : public EpoReader {
public:
    MemoryEpoReader(const uint8_t* data, size_t size) : m_data(data), m_size(size) {}

    size_t size() const override { return m_size; }
    bool read(size_t offset, uint8_t* dst, size_t n) override;

private:
    const uint8_t* m_data;
    size_t m_size;
};

/**
 * @brief: Universal EPO/QEPO/ELPO file parser
 */
class EpoParser {
public:
    /// Size of one EPO segment, and of the Galileo/BeiDou header.
    static constexpr size_t kSegmentSize = 72;

    /**
     * @brief: Default constructor
     */
    EpoParser();

    /**
     * @brief: Initialize parser from a file
     * @param: reader - the file. Must outlive every later call that reads
     *         segments (selectSegments()).
     * @param: scratch - working buffer the file is read through, a whole
     *         number of segments at a time. Must outlive the same calls.
     * @param: scratchSize - size of @p scratch in bytes; at least one segment.
     * @retval: true if initialization successful, false otherwise
     */
    bool init(EpoReader& reader, uint8_t* scratch, size_t scratchSize);

    /**
     * @brief: Get GNSS system type
     * @retval: SystemType enum value
     */
    SystemType getSystemType() const;

    /**
     * @brief: Is GNSS system type match with specified one.
     * @param: SystemType enum value
     * @retval 'true' - If GNSS system type match with specified one,
     *         'false' - othervise.
     */
    bool isSystemType(SystemType type) const;

    /**
     * @brief: Get EPO file format type
     * @retval: EpoFileType enum value
     */
    EpoFileType getFileType() const;

    /**
     * @brief: Check if file covers specified GPS time
     * @param: week - GPS week number
     * @param: tow - Time of week in seconds
     * @retval: true if time is covered, false otherwise
     */
    bool coversTime(uint32_t week, uint32_t tow) const;

    /**
     * @brief: Get header if the constellation is Galileo or Beidou
     * @retval: Raw pointer to the 72-byte header, or nullptr.
     */
    const uint8_t* getHeader() const;

    /**
     * @brief: Get total number of segments in file
     * @retval: Number of 72-byte segments
     */
    size_t getSegmentCount() const;

    /**
     * @brief: The GPS-hour window an injection for (week, tow) loads.
     * @details A QEPO file holds one 6-hour block and is regenerated hourly,
     *          so anything from five hours back is still current. Longer
     *          files are cut into fixed 6-hour blocks and only the block
     *          holding the target hour is loaded.
     * @param: minHour - first GPS hour in the window
     * @param: maxHour - first GPS hour after it
     */
    void segmentWindow(uint32_t week, uint32_t tow,
                       uint32_t& minHour, uint32_t& maxHour) const;

    /**
     * @brief: Copy out the segments an injection for (sys, week, tow) sends.
     * @details Every segment of @p sys whose hour lies in segmentWindow(), in
     *          file order. The file is read through the scratch buffer twice,
     *          once to count and once to copy, so @p out is sized exactly and
     *          nothing the size of the file is held.
     * @param: out - receives the 72-byte segments back to back; left empty
     *         when none match.
     * @retval: false if the file could not be read or @p out could not be
     *          allocated.
     */
    bool selectSegments(SystemType sys, uint32_t week, uint32_t tow, EpoBuffer& out);

    /**
     * @brief: Get start GPS time of file
     * @param: outWeek - output GPS week
     * @param: outTow - output time of week in seconds
     * @retval: true if time available, false otherwise
     */
    bool getStartTime(uint32_t& outWeek, uint32_t& outTow) const;

    /**
     * @brief: Get start GPS time of file
     * @param: gpsHour - output gpsHour
     * @retval: true if time available, false otherwise
     */
    bool getStartTime(uint32_t& gpsHour) const;

    /**
     * @brief: Get end GPS time of file
     * @param: gpsHour - output gpsHour
     * @retval: true if time available, false otherwise
     */
    bool getEndTime(uint32_t& gpsHour) const;

    /**
     * @brief: Get end GPS time of file
     * @param: outWeek - output GPS week
     * @param: outTow - output time of week in seconds
     * @retval: true if time available, false otherwise
     */
    bool getEndTime(uint32_t& outWeek, uint32_t& outTow) const;

    /**
     * @brief: Get prediction duration in hours
     * @retval: Duration in hours
     */
    uint32_t getDurationHours() const;

    /**
     * @brief: Check if parser is initialized and valid
     * @retval: true if valid, false otherwise
     */
    bool isValid() const;

    /**
     * @brief: Convert GPS hour to week and TOW
     * @param: gpsHour - GPS hour since epoch
     * @param: outWeek - output GPS week
     * @param: outTow - output time of week in seconds
     */
    void gpsHourToWeekTow(uint32_t gpsHour, uint32_t& outWeek,
                          uint32_t& outTow) const;

    /**
     * @brief: Convert week and TOW to GPS hour
     * @param: week - GPS week number
     * @param: tow - Time of week in seconds
     * @retval: GPS hour since epoch
     */
    uint32_t weekTowToGpsHour(uint32_t week, uint32_t tow) const;

    /**
     * @brief: Convert SV ID to PRN
     * @param: svId - Satellite Vehicle ID
     * @retval: Satellite PRN number
     */
    static uint8_t svIdToPRN(uint8_t svId);

private:
    EpoReader* m_reader;            // The file
    uint8_t* m_scratch;             // Buffer the file is read through
    size_t m_scratchSegments;       // Whole segments that fit in m_scratch
    size_t m_size;                  // File size
    uint8_t m_header[kSegmentSize]; // Galileo/BeiDou header, copied out at init
    EpoFileType m_fileType;         // Detected file type
    SystemType m_systemType;        // GNSS system type
    bool m_systemTypeCombinated;    // GPS+GLONASS

    uint32_t m_startGpsHour;        // Start GPS hour
    uint32_t m_endGpsHour;          // End GPS hour
    size_t m_segmentCount;          // Total segments
    size_t m_headerSize;            // Header size (0 or 72)

    uint8_t m_satelliteCount;       // Number of satellites
    uint32_t m_satelliteMask;       // Bitmask for variable formats

    bool m_initialized;             // Initialization flag

    /**
     * @brief: Detect file type from data
     * @retval: true if detection successful
     */
    bool detectFileType();

    /**
     * @brief: Parse header (for variable format files)
     * @retval: true if parsing successful
     */
    bool parseHeader();

    /**
     * @brief: Visit segments [first, first + count) in file order.
     * @details Reads through m_scratch, as many whole segments at a time as
     *          fit. @p fn is called as bool(const Segment&); returning false
     *          stops the walk early without that counting as a failure.
     * @retval: false if the file could not be read.
     */
    template <typename Fn>
    bool forEachSegment(size_t first, size_t count, Fn fn) const;

    /**
     * @brief: Decode the segment at @p raw.
     */
    Segment decodeSegment(const uint8_t* raw) const;

    /**
     * @brief: Convert SV ID to system type
     * @param: svId - Satellite Vehicle ID
     * @retval: SystemType enum value
     */
    static SystemType svIdToSystemType(uint8_t svId);

    /**
     * @brief: Read 24-bit little-endian GPS hour from the start of a segment
     * @param: raw - the segment
     * @retval: GPS hour value
     */
    static uint32_t readGpsHour(const uint8_t* raw);

    /**
     * @brief: Read 32-bit little-endian value
     * @param: raw - the bytes
     * @retval: The value
     */
    static uint32_t readLe32(const uint8_t* raw);

    /**
     * @brief: Count set bits in satellite mask
     * @param: mask - 32-bit satellite mask
     * @retval: Number of set bits
     */
    static uint8_t countSetBits(uint32_t mask);

    /**
     * @brief: Get system ID from SV ID
     * @param: svId - Satellite Vehicle ID
     * @retval: System ID (0-4)
     */
    static uint8_t getSystemId(uint8_t svId);
};

} // namespace Airoha

