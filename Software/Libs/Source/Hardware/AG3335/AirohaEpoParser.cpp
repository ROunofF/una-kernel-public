/**
 ******************************************************************************
 * @file    AirohaEpoParser.cpp
 * @brief   Universal parser for EPO/QEPO/ELPO/QELPO files.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "AG3335/AirohaEpoParser.hpp"
#include <cstring>

namespace Airoha
{

 // Constants
static constexpr size_t SEGMENT_SIZE = EpoParser::kSegmentSize;
static constexpr size_t HEADER_SIZE = EpoParser::kSegmentSize;

// EpoData carries segments selected here, so the two must agree on what one is.
static_assert(EpoData::kSegmentSize == EpoParser::kSegmentSize,
              "EpoData and EpoParser disagree on the EPO segment size");
static constexpr uint32_t HOURS_PER_WEEK = 168;
static constexpr uint32_t SECONDS_PER_HOUR = 3600;

template <typename Fn>
bool EpoParser::forEachSegment(size_t first, size_t count, Fn fn) const
{
    size_t index = first;
    const size_t end = first + count;

    while (index < end) {
        const size_t left = end - index;
        const size_t chunk = (left < m_scratchSegments) ? left : m_scratchSegments;

        if (!m_reader->read(m_headerSize + index * SEGMENT_SIZE, m_scratch,
                chunk * SEGMENT_SIZE)) {
            return false;
        }

        for (size_t i = 0; i < chunk; ++i) {
            if (!fn(decodeSegment(m_scratch + i * SEGMENT_SIZE))) {
                return true;
            }
        }

        index += chunk;
    }

    return true;
}

/**
 * @brief: Default constructor
 */
bool MemoryEpoReader::read(size_t offset, uint8_t* dst, size_t n)
{
    if (m_data == nullptr || offset > m_size || n > m_size - offset) {
        return false;
    }
    std::memcpy(dst, m_data + offset, n);
    return true;
}

EpoParser::EpoParser()
    : m_reader(nullptr)
    , m_scratch(nullptr)
    , m_scratchSegments(0)
    , m_size(0)
    , m_header {}
    , m_fileType(EpoFileType::UNKNOWN)
    , m_systemType(SystemType::UNKNOWN)
    , m_systemTypeCombinated(false)
    , m_startGpsHour(0)
    , m_endGpsHour(0)
    , m_segmentCount(0)
    , m_headerSize(0)
    , m_satelliteCount(0)
    , m_satelliteMask(0)
    , m_initialized(false)
{}

/**
 * @brief: Initialize parser from a file
 * @param: reader - the file
 * @param: scratch - buffer the file is read through
 * @param: scratchSize - size of scratch in bytes
 * @retval: true if initialization successful, false otherwise
 */
bool EpoParser::init(EpoReader& reader, uint8_t* scratch, size_t scratchSize)
{
    // Reset state
    m_initialized = false;
    m_reader = nullptr;
    m_scratch = nullptr;
    m_scratchSegments = 0;
    m_size = 0;

    const size_t size = reader.size();

    // Validate input
    if (!scratch || scratchSize < SEGMENT_SIZE || size < SEGMENT_SIZE) {
        return false;
    }

    // Check if size is multiple of 72 bytes
    if (size % SEGMENT_SIZE != 0) {
        return false;
    }

    m_reader = &reader;
    m_scratch = scratch;
    m_scratchSegments = scratchSize / SEGMENT_SIZE;
    m_size = size;

    // Detect file type and parse structure
    if (!detectFileType()) {
        return false;
    }

    m_initialized = true;
    return true;
}

/**
 * @brief: Detect file type from data
 * @retval: true if detection successful
 */
bool EpoParser::detectFileType()
{
    // Read first SV ID. The first 72 bytes are the header in a variable-format
    // file and the first segment otherwise; either way they are kept, and the
    // header is what getHeader() hands out later.
    if (!m_reader->read(0, m_header, SEGMENT_SIZE)) {
        return false;
    }
    uint8_t firstSvId = m_header[3];

    // Check for header (Galileo SVID=254, BeiDou SVID=255)
    if (firstSvId == 254 || firstSvId == 255) {
        // Variable format with header
        m_headerSize = HEADER_SIZE;

        if (!parseHeader()) {
            return false;
        }

        // Determine system type from header
        m_systemType = (firstSvId == 254) ? SystemType::GALILEO : SystemType::BEIDOU;

        // Calculate segments based on file size
        size_t dataSize = m_size - m_headerSize;
        m_segmentCount = dataSize / SEGMENT_SIZE;

        // Count actual unique SV IDs in file (more reliable than mask)
        bool foundSvIds[256] = { false };
        size_t actualSatCount = 0;
        const size_t toScan = (m_segmentCount < 2000) ? m_segmentCount : 2000;
        if (!forEachSegment(0, toScan, [&](const Segment& seg) {
                if (!foundSvIds[seg.svId]) {
                    foundSvIds[seg.svId] = true;
                    actualSatCount++;
                }
                return true;
            })) {
            return false;
        }

        // Use actual count if reasonable, otherwise fall back to mask
        if (actualSatCount > m_satelliteCount && actualSatCount < 100) {
            m_satelliteCount = actualSatCount;
        }

        // First hour of the data, which follows the header. Zero for a file
        // that is nothing but a header, as the bounds check used to give.
        uint32_t firstHour = 0;
        if (m_segmentCount > 0) {
            uint8_t first[SEGMENT_SIZE];
            if (!m_reader->read(m_headerSize, first, SEGMENT_SIZE)) {
                return false;
            }
            firstHour = readGpsHour(first);
        }

        // Detect EPO vs QEPO/ELPO based on segment count
        size_t segmentsPerSat = m_segmentCount / m_satelliteCount;

        if (segmentsPerSat == 1) {
            // QEPO/QELPO - single 6-hour segment
            m_fileType = EpoFileType::QEPO_VARIABLE;
            m_startGpsHour = firstHour;
            m_endGpsHour = m_startGpsHour + 6;
        } else if (segmentsPerSat == 12) {
            // ELPO - 3 days (12 segments = 4 per day -> 3 days)
            m_fileType = EpoFileType::ELPO_VARIABLE;
            m_startGpsHour = firstHour;
            m_endGpsHour = m_startGpsHour + 72;
        } else if (segmentsPerSat == 28) {
            // ELPO - 7 days (28 segments = 4 per day -> 7 days)
            m_fileType = EpoFileType::ELPO_VARIABLE;
            m_startGpsHour = firstHour;
            m_endGpsHour = m_startGpsHour + 168;
        } else if (segmentsPerSat >= 60) {
            // EPO - long-term (15 days = 60 segments)
            m_fileType = EpoFileType::EPO_VARIABLE;
            m_startGpsHour = firstHour;
            m_endGpsHour = m_startGpsHour + (segmentsPerSat / 4) * 24;
        } else {
            // ELPO - other duration (between 3-15 days)
            m_fileType = EpoFileType::ELPO_VARIABLE;
            m_startGpsHour = firstHour;
            m_endGpsHour = m_startGpsHour + (segmentsPerSat / 4) * 24;
        }
    } else {
        // Fixed format without header
        m_headerSize = 0;
        m_segmentCount = m_size / SEGMENT_SIZE;

        // Scan multiple segments to detect constellation mix
        bool hasGps = false;
        bool hasGlonass = false;
        bool hasQzss = false;

        size_t samplesToCheck = (m_segmentCount < 100) ? m_segmentCount : 100;
        if (!forEachSegment(0, samplesToCheck, [&](const Segment& seg) {
                SystemType sysType = svIdToSystemType(seg.svId);
                if (sysType == SystemType::GPS) hasGps = true;
                if (sysType == SystemType::GLONASS) hasGlonass = true;
                if (sysType == SystemType::QZSS) hasQzss = true;
                return true;
            })) {
            return false;
        }

        // Determine constellation
        if (hasGps && hasGlonass) {
            m_systemType = SystemType::GPS; // GPS+GLONASS combined
            m_systemTypeCombinated = true;
        } else if (hasQzss) {
            m_systemType = SystemType::QZSS;
        } else {
            m_systemType = svIdToSystemType(firstSvId);
        }

        // Read first GPS hour
        m_startGpsHour = readGpsHour(m_header);

        // Detect format based on segment count
        if (m_segmentCount == 32) {
            // QEPO GPS - 32 satellites, 6 hours
            m_fileType = EpoFileType::QEPO_FIXED;
            m_satelliteCount = 32;
            m_endGpsHour = m_startGpsHour + 6;
        } else if (m_segmentCount == 56) {
            // QEPO GPS+GLONASS - 56 satellites, 6 hours
            m_fileType = EpoFileType::QEPO_FIXED;
            m_satelliteCount = 56;
            m_endGpsHour = m_startGpsHour + 6;
        } else if (m_segmentCount == 24) {
            // QELPO GLONASS only - 24 satellites, 6 hours
            m_fileType = EpoFileType::QEPO_FIXED;
            m_satelliteCount = 24;
            m_systemType = SystemType::GLONASS;
            m_endGpsHour = m_startGpsHour + 6;
        } else {
            // EPO or ELPO format
            // Try to determine satellite count from structure
            // Typical: 32 GPS, 56 GPS+GLO, 7 QZSS

            // Check if QZSS (SV 33-39)
            if (firstSvId >= 33 && firstSvId <= 39) {
                m_satelliteCount = 7;
                m_systemType = SystemType::QZSS;
                size_t segmentsPerSat = m_segmentCount / 7;

                if (segmentsPerSat == 12) {
                    // 3-day ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + 72;
                } else if (segmentsPerSat == 28) {
                    // 7-day ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + 168;
                } else {
                    // Other durations
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    size_t days = segmentsPerSat / 4;
                    m_endGpsHour = m_startGpsHour + days * 24;
                }
            } else if (hasGps && hasGlonass && m_segmentCount % 56 == 0) {
                // GPS+GLONASS (check constellation mix first!)
                m_satelliteCount = 56;
                size_t segmentsPerSat = m_segmentCount / 56;

                if (segmentsPerSat == 12) {
                    // 3-day ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + 72;
                } else if (segmentsPerSat == 28) {
                    // 7-day ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + 168;
                } else if (segmentsPerSat >= 60) {
                    // Long-term EPO (15 days)
                    m_fileType = EpoFileType::EPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + (segmentsPerSat / 4) * 24;
                } else {
                    // Medium-term ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + (segmentsPerSat / 4) * 24;
                }
            } else if (m_segmentCount % 24 == 0 && !hasGps) {
                // GLONASS only (24 satellites)
                m_satelliteCount = 24;
                m_systemType = SystemType::GLONASS;
                size_t segmentsPerSat = m_segmentCount / 24;

                if (segmentsPerSat == 12) {
                    // 3-day ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + 72;
                } else if (segmentsPerSat == 28) {
                    // 7-day ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + 168;
                } else if (segmentsPerSat >= 60) {
                    // Long-term EPO (15 days)
                    m_fileType = EpoFileType::EPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + (segmentsPerSat / 4) * 24;
                } else {
                    // Medium-term ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + (segmentsPerSat / 4) * 24;
                }
            } else if (m_segmentCount % 32 == 0) {
                // GPS only
                m_satelliteCount = 32;
                size_t segmentsPerSat = m_segmentCount / 32;

                if (segmentsPerSat == 12) {
                    // 3-day ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + 72;
                } else if (segmentsPerSat == 28) {
                    // 7-day ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + 168;
                } else if (segmentsPerSat >= 60) {
                    // Long-term EPO (15 days)
                    m_fileType = EpoFileType::EPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + (segmentsPerSat / 4) * 24;
                } else {
                    // Medium-term ELPO
                    m_fileType = EpoFileType::ELPO_STANDARD;
                    m_endGpsHour = m_startGpsHour + (segmentsPerSat / 4) * 24;
                }
            } else {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief: Parse header (for variable format files)
 * @retval: true if parsing successful
 */
bool EpoParser::parseHeader()
{
    if (m_size < HEADER_SIZE) {
        return false;
    }

    // Read satellite mask from bytes 4-7
    m_satelliteMask = readLe32(m_header + 4);

    // Count available satellites
    m_satelliteCount = countSetBits(m_satelliteMask);

    return (m_satelliteCount > 0);
}

/**
 * @brief: Get base GNSS system type
 * @retval: SystemType enum value
 */
SystemType EpoParser::getSystemType() const
{
    return m_systemType;
}

/**
 * @brief: Is GNSS system type match with specified one.
 * @param: SystemType enum value
 * @retval 'true' - If GNSS system type match with specified one,
 *         'false' - othervise.
 */
bool EpoParser::isSystemType(SystemType type) const
{
    if (type == SystemType::GLONASS && m_systemType == SystemType::GPS && m_systemTypeCombinated) {
        return true;
    }

    return (type == m_systemType);
}

/**
 * @brief: Get EPO file format type
 * @retval: EpoFileType enum value
 */
EpoFileType EpoParser::getFileType() const
{
    return m_fileType;
}

/**
 * @brief: Check if file covers specified GPS time
 * @param: week - GPS week number
 * @param: tow - Time of week in seconds
 * @retval: true if time is covered, false otherwise
 */
bool EpoParser::coversTime(uint32_t week, uint32_t tow) const
{
    if (!m_initialized) {
        return false;
    }

    uint32_t targetGpsHour = weekTowToGpsHour(week, tow);

    return (targetGpsHour >= m_startGpsHour && targetGpsHour < m_endGpsHour);
}

/**
 * @brief: Get header if the constellation is Galileo or Beidou
 * @retval: Raw pointer to the 72-byte header, or nullptr.
 */
const uint8_t* EpoParser::getHeader() const
{
    if (m_systemType == SystemType::GALILEO || m_systemType == SystemType::BEIDOU) {
        return m_header;
    }

    return nullptr;
}

/**
 * @brief: Get total number of segments in file
 * @retval: Number of 72-byte segments
 */
size_t EpoParser::getSegmentCount() const
{
    return m_segmentCount;
}

/**
 * @brief: Decode the segment at raw
 * @param: raw - the 72-byte segment
 * @retval: The decoded segment, pointing at raw
 */
Segment EpoParser::decodeSegment(const uint8_t* raw) const
{
    Segment seg {};
    seg.raw = raw;
    seg.gpsHour = readGpsHour(raw);
    seg.svId = raw[3];
    seg.prn = svIdToPRN(seg.svId);
    seg.systemId = getSystemId(seg.svId);
    seg.checksum = readLe32(raw + 68);
    return seg;
}

void EpoParser::segmentWindow(uint32_t week, uint32_t tow,
    uint32_t& minHour, uint32_t& maxHour) const
{
    uint32_t targetHour = weekTowToGpsHour(week, tow);

    minHour = targetHour;
    maxHour = targetHour + 6;
    // A QEPO file covers the hours before the target too, so its window
    // starts five hours earlier. Other EPO files use 6-hour segments.
    if (m_fileType == EpoFileType::QEPO_FIXED || m_fileType == EpoFileType::QEPO_VARIABLE) {
        // Saturated: in the first five hours of GPS time this would wrap and
        // select nothing. No real capsule starts there, but the window should
        // not depend on that.
        minHour = (targetHour >= 5) ? targetHour - 5 : 0;
    } else {
        // Determine which 6-hour segment
        minHour = (targetHour / 6) * 6;
        maxHour = minHour + 6;
    }
}

bool EpoParser::selectSegments(SystemType sys, uint32_t week, uint32_t tow, EpoBuffer& out)
{
    (void)out.resize(0);

    if (!m_initialized) {
        return false;
    }

    uint32_t minHour = 0;
    uint32_t maxHour = 0;
    segmentWindow(week, tow, minHour, maxHour);

    auto wanted = [&](const Segment& seg) {
        return static_cast<SystemType>(seg.systemId) == sys
                && seg.gpsHour >= minHour && seg.gpsHour < maxHour;
    };

    // Count first, so the buffer is exactly the size of what is sent.
    size_t count = 0;
    if (!forEachSegment(0, m_segmentCount, [&](const Segment& seg) {
            if (wanted(seg)) {
                ++count;
            }
            return true;
        })) {
        return false;
    }

    if (count == 0) {
        return true;
    }

    if (!out.reserve(count * SEGMENT_SIZE)) {
        return false;
    }

    // The second pass must find the same segments: nothing else writes the
    // file while the caller holds it, but stop at the count regardless, so a
    // file that changed underneath can never write past the buffer.
    size_t copied = 0;
    if (!forEachSegment(0, m_segmentCount, [&](const Segment& seg) {
            if (wanted(seg)) {
                std::memcpy(out.data() + copied * SEGMENT_SIZE, seg.raw, SEGMENT_SIZE);
                ++copied;
            }
            return copied < count;
        })) {
        return false;
    }

    return out.resize(copied * SEGMENT_SIZE);
}

bool EpoParser::getStartTime(uint32_t& outWeek, uint32_t& outTow) const
{
    if (!m_initialized) {
        return false;
    }

    gpsHourToWeekTow(m_startGpsHour, outWeek, outTow);
    return true;
}


bool EpoParser::getStartTime(uint32_t& gpsHour) const
{
    if (!m_initialized) {
        return false;
    }
    gpsHour = m_startGpsHour;
    return true;
}

bool EpoParser::getEndTime(uint32_t& outWeek, uint32_t& outTow) const
{
    if (!m_initialized) {
        return false;
    }

    gpsHourToWeekTow(m_endGpsHour, outWeek, outTow);
    return true;
}

bool EpoParser::getEndTime(uint32_t& gpsHour) const
{
    if (!m_initialized) {
        return false;
    }
    gpsHour = m_endGpsHour;
    return true;
}

/**
 * @brief: Get prediction duration in hours
 * @retval: Duration in hours
 */
uint32_t EpoParser::getDurationHours() const
{
    return m_endGpsHour - m_startGpsHour;
}

/**
 * @brief: Check if parser is initialized and valid
 * @retval: true if valid, false otherwise
 */
bool EpoParser::isValid() const
{
    return m_initialized;
}

/**
 * @brief: Convert GPS hour to week and TOW
 * @param: gpsHour - GPS hour since epoch
 * @param: outWeek - output GPS week
 * @param: outTow - output time of week in seconds
 */
void EpoParser::gpsHourToWeekTow(uint32_t gpsHour, uint32_t& outWeek,
    uint32_t& outTow) const
{
    outWeek = gpsHour / HOURS_PER_WEEK;
    uint32_t hourInWeek = gpsHour % HOURS_PER_WEEK;
    outTow = hourInWeek * SECONDS_PER_HOUR;
}

/**
 * @brief: Convert week and TOW to GPS hour
 * @param: week - GPS week number
 * @param: tow - Time of week in seconds
 * @retval: GPS hour since epoch
 */
uint32_t EpoParser::weekTowToGpsHour(uint32_t week, uint32_t tow) const
{
    uint32_t hourInWeek = tow / SECONDS_PER_HOUR;
    return week * HOURS_PER_WEEK + hourInWeek;
}

/**
 * @brief: Convert SV ID to system type
 * @param: svId - Satellite Vehicle ID
 * @retval: SystemType enum value
 */
SystemType EpoParser::svIdToSystemType(uint8_t svId)
{
    if (svId >= 1 && svId <= 32) {
        return SystemType::GPS;
    } else if (svId >= 33 && svId <= 39) {
        return SystemType::QZSS;
    } else if (svId >= 65 && svId <= 88) {
        return SystemType::GLONASS;
    } else if (svId >= 101 && svId <= 136) {
        return SystemType::GALILEO;
    } else if ((svId >= 190 && svId <= 198) || (svId >= 201 && svId <= 254)) {
        return SystemType::BEIDOU;
    }
    return SystemType::UNKNOWN;
}

uint8_t EpoParser::svIdToPRN(uint8_t svId)
{
    if (svId >= 1 && svId <= 32) {
        return svId;
    } else if (svId >= 33 && svId <= 39) {
        return svId - 32; // + 160
    } else if (svId >= 65 && svId <= 88) {
        return svId - 64;
    } else if (svId >= 101 && svId <= 136) {
        return svId - 100;
    } else if (svId >= 190 && svId <= 198) {
        return svId - 135;
    } else if (svId >= 201 && svId <= 254) {
        return svId - 200;
    }
    return 0xFF;
}


/**
 * @brief: Read 24-bit little-endian GPS hour from the start of a segment
 * @param: raw - the segment
 * @retval: GPS hour value
 */
uint32_t EpoParser::readGpsHour(const uint8_t* raw)
{
    return (static_cast<uint32_t>(raw[0]) << 0) |
        (static_cast<uint32_t>(raw[1]) << 8) |
        (static_cast<uint32_t>(raw[2]) << 16);
}

/**
 * @brief: Read 32-bit little-endian value
 * @param: raw - the bytes
 * @retval: The value
 */
uint32_t EpoParser::readLe32(const uint8_t* raw)
{
    return (static_cast<uint32_t>(raw[0]) << 0) |
        (static_cast<uint32_t>(raw[1]) << 8) |
        (static_cast<uint32_t>(raw[2]) << 16) |
        (static_cast<uint32_t>(raw[3]) << 24);
}

/**
 * @brief: Count set bits in satellite mask
 * @param: mask - 32-bit satellite mask
 * @retval: Number of set bits
 */
uint8_t EpoParser::countSetBits(uint32_t mask)
{
    uint8_t count = 0;
    while (mask) {
        count += mask & 1;
        mask >>= 1;
    }
    return count;
}

/**
 * @brief: Get system ID from SV ID
 * @param: svId - Satellite Vehicle ID
 * @retval: System ID (0-4)
 */
uint8_t EpoParser::getSystemId(uint8_t svId)
{
    SystemType sys = svIdToSystemType(svId);
    return static_cast<uint8_t>(sys);
}

} // namespace Airoha
