/**
 ******************************************************************************
 * @file    AirohaNmeaParser.cpp
 * @brief   NMEA parser.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "AG3335/AirohaNmeaParser.hpp"

#include <cstddef>
#include <cstdlib>
#include <charconv>

namespace Airoha::Nmea
{

/**
 * @brief Checks whether the given character is considered whitespace (ASCII-only).
 * @param c Character to check.
 * @return true if space or tab, false otherwise.
 */
static inline bool IsSpace(char c) {
    return c == ' ' || c == '\t';
}

/**
 * @brief Removes leading and trailing ASCII spaces and tabs.
 * @param v Token view to Trim.
 * @return Trimmed view.
 */
static inline std::string_view Trim(std::string_view v)
{
    while (!v.empty() && IsSpace(v.front())) v.remove_prefix(1);
    while (!v.empty() && IsSpace(v.back()))  v.remove_suffix(1);
    return v;
}

/**
 * @brief Determine GNSS system from sentence type
 * @param type NMEA sentence type
 * @return GNSS system type
 */
static inline GnssSystem GetGnssSystem(NmeaSentenceType type)
{
    switch (type) {
        case NmeaSentenceType::GPGGA:
        case NmeaSentenceType::GPRMC:
        case NmeaSentenceType::GPGSV:
        case NmeaSentenceType::GPGLL:
        case NmeaSentenceType::GPGSA:
        case NmeaSentenceType::GPVTG:
            return GnssSystem::GPS;

        case NmeaSentenceType::GLGGA:
        case NmeaSentenceType::GLRMC:
        case NmeaSentenceType::GLGSV:
            return GnssSystem::GLONASS;

        case NmeaSentenceType::GAGGA:
        case NmeaSentenceType::GARMC:
        case NmeaSentenceType::GAGSV:
            return GnssSystem::GALILEO;

        case NmeaSentenceType::BDGGA:
        case NmeaSentenceType::BDRMC:
        case NmeaSentenceType::GBGSV:
            return GnssSystem::BEIDOU;

        case NmeaSentenceType::GNGGA:
        case NmeaSentenceType::GNRMC:
        case NmeaSentenceType::GNGSV:
            return GnssSystem::MULTI;

        default:
            return GnssSystem::UNKNOWN;
    }
}

/**
 * @brief Identify NMEA sentence type from first token
 * @param token First token of NMEA sentence (e.g., "GPGGA", "GPRMC")
 * @return NmeaSentenceType enum value
 *
 * @note This function performs case-sensitive comparison.
 *       Standard NMEA sentences should be uppercase.
 *
 * Example:
 *   std::string_view token = "GPGGA";
 *   NmeaSentenceType type = IdentifyNmeaSentence(token);
 *   if (type == NmeaSentenceType::GPGGA) {
 *       // Parse GGA sentence
 *   }
 */
static inline NmeaSentenceType IdentifyNmeaSentence(std::string_view token)
{
    // Check minimum length
    if (token.size() < 5) {
        return NmeaSentenceType::UNKNOWN;
    }

    // GPS (GP prefix)
    if (token == "GPGGA") return NmeaSentenceType::GPGGA;
    if (token == "GPRMC") return NmeaSentenceType::GPRMC;
    if (token == "GPGSV") return NmeaSentenceType::GPGSV;
    if (token == "GPGLL") return NmeaSentenceType::GPGLL;
    if (token == "GPGSA") return NmeaSentenceType::GPGSA;
    if (token == "GPVTG") return NmeaSentenceType::GPVTG;

    // Multi-GNSS (GN prefix)
    if (token == "GNGGA") return NmeaSentenceType::GNGGA;
    if (token == "GNRMC") return NmeaSentenceType::GNRMC;
    if (token == "GNGSV") return NmeaSentenceType::GNGSV;

    // GLONASS (GL prefix)
    if (token == "GLGGA") return NmeaSentenceType::GLGGA;
    if (token == "GLRMC") return NmeaSentenceType::GLRMC;
    if (token == "GLGSV") return NmeaSentenceType::GLGSV;

    // Galileo (GA prefix)
    if (token == "GAGGA") return NmeaSentenceType::GAGGA;
    if (token == "GARMC") return NmeaSentenceType::GARMC;
    if (token == "GAGSV") return NmeaSentenceType::GAGSV;

    // BeiDou (BD/GB prefix)
    if (token == "BDGGA") return NmeaSentenceType::BDGGA;
    if (token == "BDRMC") return NmeaSentenceType::BDRMC;
    if (token == "GBGSV") return NmeaSentenceType::GBGSV;

    return NmeaSentenceType::UNKNOWN;
}


static inline bool ParseGGA(std::string_view* tokens, size_t count, GGA &data, NmeaSentenceType type)
{
    // Expected format: $GPGGA,time,lat,N/S,lon,E/W,quality,numSV,HDOP,alt,M,sep,M,diffAge,diffStation
    // Minimum tokens: GPGGA + 14 fields = 15 tokens
    if (count < 15) {
        return false;
    }

    // Token 0 is "GPGGA" - already verified by caller

    // Set GNSS system type
    data.system = GetGnssSystem(type);

    // Parse UTC time (token 1) - format: hhmmss.sss
    if (!tokens[1].empty()) {
        char* end_ptr = nullptr;
        data.utc_time = std::strtof(tokens[1].data(), &end_ptr);
        if (end_ptr == tokens[1].data()) {
            return false;
        }
    } else {
        data.utc_time = 0.0f;
    }

    // Parse latitude (token 2) - format: ddmm.mmmm
    if (!tokens[2].empty()) {
        char* end_ptr = nullptr;
        float lat_raw = std::strtof(tokens[2].data(), &end_ptr);
        if (end_ptr == tokens[2].data()) {
            return false;
        }

        // N/S indicator (token 3) must be present and exactly 'N' or 'S'.
        // A coordinate without a hemisphere is ambiguous; defaulting the sign
        // can silently flip an otherwise valid fix, so reject the sentence.
        if (tokens[3].size() != 1 || (tokens[3][0] != 'N' && tokens[3][0] != 'S')) {
            return false;
        }
        data.latitude = NmeaCoordToDecimal(lat_raw, tokens[3][0]);
    } else {
        data.latitude = 0.0f;
    }

    // Parse longitude (token 4) - format: dddmm.mmmm
    if (!tokens[4].empty()) {
        char* end_ptr = nullptr;
        float lon_raw = std::strtof(tokens[4].data(), &end_ptr);
        if (end_ptr == tokens[4].data()) {
            return false;
        }

        // E/W indicator (token 5) must be present and exactly 'E' or 'W'.
        if (tokens[5].size() != 1 || (tokens[5][0] != 'E' && tokens[5][0] != 'W')) {
            return false;
        }
        data.longitude = NmeaCoordToDecimal(lon_raw, tokens[5][0]);
    } else {
        data.longitude = 0.0f;
    }

    // Parse fix quality (token 6)
    int quality_val = 0;
    auto result = std::from_chars(tokens[6].data(),
                                  tokens[6].data() + tokens[6].size(),
                                  quality_val);
    if (result.ec != std::errc() || quality_val < 0 || quality_val > 8) {
        return false;
    }
    data.fix_quality = static_cast<GGA::FixQuality>(quality_val);

    // Parse number of satellites (token 7)
    int num_sats = 0;
    result = std::from_chars(tokens[7].data(),
                            tokens[7].data() + tokens[7].size(),
                            num_sats);
    if (result.ec != std::errc() || num_sats < 0) {
        return false;
    }
    data.num_satellites = static_cast<uint8_t>(num_sats);

    // Parse HDOP (token 8)
    if (!tokens[8].empty()) {
        char* end_ptr = nullptr;
        data.hdop = std::strtof(tokens[8].data(), &end_ptr);
        if (end_ptr == tokens[8].data()) {
            return false;
        }
    } else {
        data.hdop = 0.0f;
    }

    // Parse altitude (token 9)
    if (!tokens[9].empty()) {
        char* end_ptr = nullptr;
        data.altitude = std::strtof(tokens[9].data(), &end_ptr);
        if (end_ptr == tokens[9].data()) {
            return false;
        }
    } else {
        data.altitude = 0.0f;
    }
    // Token 10 is 'M' - altitude unit, skip

    // Parse geoid separation (token 11)
    if (!tokens[11].empty()) {
        char* end_ptr = nullptr;
        data.geoid_sep = std::strtof(tokens[11].data(), &end_ptr);
        if (end_ptr == tokens[11].data()) {
            return false;
        }
    } else {
        data.geoid_sep = 0.0f;
    }
    // Token 12 is 'M' - geoid separation unit, skip
    // Token 13 is differential age - optional, skip
    // Token 14 is differential station ID - optional, skip

    return true;
}

static inline bool ParseRMC(std::string_view* tokens, size_t count, RMC &data, NmeaSentenceType type)
{
    // Expected format: $GPRMC,time,status,lat,N/S,lon,E/W,speed,track,date,magvar,E/W,mode
    // Minimum tokens: GPRMC + 11 fields = 12 tokens (mode is optional)
    if (count < 12) {
        return false;
    }

    // Token 0 is "GPRMC" - already verified by caller

    // Set GNSS system type
    data.system = GetGnssSystem(type);

    // Parse UTC time (token 1) - format: hhmmss.sss
    if (!tokens[1].empty()) {
        char* end_ptr = nullptr;
        data.utc_time = std::strtof(tokens[1].data(), &end_ptr);
        if (end_ptr == tokens[1].data()) {
            return false;
        }
    } else {
        data.utc_time = 0.0f;
    }

    // Parse status (token 2) - 'A' = active/valid, 'V' = void/invalid
    if (tokens[2].empty()) {
        return false;
    }
    if (tokens[2][0] == 'A') {
        data.status = RMC::Status::VALID;
    } else if (tokens[2][0] == 'V') {
        data.status = RMC::Status::INVALID;
    } else {
        return false;
    }

    // Parse latitude (token 3) - format: ddmm.mmmm
    if (!tokens[3].empty()) {
        char* end_ptr = nullptr;
        float lat_raw = std::strtof(tokens[3].data(), &end_ptr);
        if (end_ptr == tokens[3].data()) {
            return false;
        }

        // N/S indicator (token 4) must be present and exactly 'N' or 'S'.
        // A coordinate without a hemisphere is ambiguous; defaulting the sign
        // can silently flip an otherwise valid fix, so reject the sentence.
        if (tokens[4].size() != 1 || (tokens[4][0] != 'N' && tokens[4][0] != 'S')) {
            return false;
        }
        data.latitude = NmeaCoordToDecimal(lat_raw, tokens[4][0]);
    } else {
        data.latitude = 0.0f;
    }

    // Parse longitude (token 5) - format: dddmm.mmmm
    if (!tokens[5].empty()) {
        char* end_ptr = nullptr;
        float lon_raw = std::strtof(tokens[5].data(), &end_ptr);
        if (end_ptr == tokens[5].data()) {
            return false;
        }

        // E/W indicator (token 6) must be present and exactly 'E' or 'W'.
        if (tokens[6].size() != 1 || (tokens[6][0] != 'E' && tokens[6][0] != 'W')) {
            return false;
        }
        data.longitude = NmeaCoordToDecimal(lon_raw, tokens[6][0]);
    } else {
        data.longitude = 0.0f;
    }

    // Parse speed over ground in knots (token 7)
    if (!tokens[7].empty()) {
        char* end_ptr = nullptr;
        data.speed_knots = std::strtof(tokens[7].data(), &end_ptr);
        if (end_ptr == tokens[7].data()) {
            return false;
        }
    } else {
        data.speed_knots = 0.0f;
    }

    // Parse track angle in degrees (token 8)
    if (!tokens[8].empty()) {
        char* end_ptr = nullptr;
        data.track_angle = std::strtof(tokens[8].data(), &end_ptr);
        if (end_ptr == tokens[8].data()) {
            return false;
        }
    } else {
        data.track_angle = 0.0f;
    }

    // Parse date (token 9) - format: ddmmyy
    if (!tokens[9].empty()) {
        auto result = std::from_chars(tokens[9].data(),
                                      tokens[9].data() + tokens[9].size(),
                                      data.date);
        if (result.ec != std::errc()) {
            return false;
        }
    } else {
        data.date = 0;
    }

    // Parse magnetic variation (token 10)
    if (!tokens[10].empty()) {
        char* end_ptr = nullptr;
        data.mag_variation = std::strtof(tokens[10].data(), &end_ptr);
        if (end_ptr == tokens[10].data()) {
            return false;
        }

        // Apply E/W indicator (token 11)
        if (count > 11 && !tokens[11].empty() && tokens[11][0] == 'W') {
            data.mag_variation = -data.mag_variation;
        }
    } else {
        data.mag_variation = 0.0f;
    }

    // Parse mode indicator (token 12) - optional in NMEA 2.3+
    if (count > 12 && !tokens[12].empty()) {
        char mode_char = tokens[12][0];
        switch (mode_char) {
            case 'A': data.mode = RMC::Mode::AUTONOMOUS; break;
            case 'D': data.mode = RMC::Mode::DIFFERENTIAL; break;
            case 'E': data.mode = RMC::Mode::ESTIMATED; break;
            case 'M': data.mode = RMC::Mode::MANUAL; break;
            case 'S': data.mode = RMC::Mode::SIMULATED; break;
            case 'N': data.mode = RMC::Mode::NOT_VALID; break;
            case 'P': data.mode = RMC::Mode::PRECISE; break;
            default: data.mode = RMC::Mode::NOT_VALID; break;
        }
    } else {
        data.mode = RMC::Mode::NOT_VALID;
    }

    return true;
}

static inline bool ParseGSV(std::string_view* tokens, size_t count, GSV &data, NmeaSentenceType type)
{
    // Expected format: $GPGSV,total_msgs,msg_num,total_sats,[sat_info]*4
    // Minimum tokens: GPGSV + 3 fields = 4 tokens
    // Maximum tokens: GPGSV + 3 fields + 4 satellites * 4 fields = 20 tokens
    if (count < 4) {
        return false;
    }

    // Token 0 is "GPGSV/GLGSV/GAGSV/GBGSV/GNGSV" - already verified by caller

    // Set GNSS system type
    data.system = GetGnssSystem(type);

    // Parse total messages (token 1)
    int total_msgs = 0;
    auto result = std::from_chars(tokens[1].data(),
                                  tokens[1].data() + tokens[1].size(),
                                  total_msgs);
    if (result.ec != std::errc() || total_msgs < 1 || total_msgs > 9) {
        return false;
    }
    data.total_messages = static_cast<uint8_t>(total_msgs);

    // Parse message number (token 2)
    int msg_num = 0;
    result = std::from_chars(tokens[2].data(),
                            tokens[2].data() + tokens[2].size(),
                            msg_num);
    if (result.ec != std::errc() || msg_num < 1 || msg_num > 9) {
        return false;
    }
    data.message_number = static_cast<uint8_t>(msg_num);

    // Parse total satellites (token 3)
    int total_sats = 0;
    result = std::from_chars(tokens[3].data(),
                            tokens[3].data() + tokens[3].size(),
                            total_sats);
    if (result.ec != std::errc() || total_sats < 0 || total_sats > 99) {
        return false;
    }
    data.total_satellites = static_cast<uint8_t>(total_sats);

    // Special case for Airoha / MediaTek:
    // When total_sats == 0, sentence may contain a stray token before checksum.
    // Ignore all satellite info and return.
    if (data.total_satellites == 0) {
        data.num_satellites = 0;
        return true;
    }

    // Calculate expected satellites in THIS message (not total)
    uint8_t sats_in_this_msg = std::min(4,
        static_cast<int>(data.total_satellites - (data.message_number - 1) * 4));

    // Only validate we have enough tokens for satellites that should be in THIS message
    size_t required = 4 + sats_in_this_msg * 4;
    if (count < required) {
        return false;
    }


    // Parse satellite information (up to 4 satellites)
    data.num_satellites = 0;
    for (uint8_t i = 0; i < 4 && data.num_satellites < data.total_satellites; i++) {
        size_t base_idx = 4 + (i * 4);

        // Check if we have enough tokens for this satellite
        if (base_idx >= count) {
            break;
        }

        // Check if PRN field is empty (no more satellites)
        if (tokens[base_idx].empty()) {
            break;
        }

        SatelliteInfo& sat = data.satellites[i];

        // Parse PRN (satellite ID)
        int prn = 0;
        result = std::from_chars(tokens[base_idx].data(),
                                tokens[base_idx].data() + tokens[base_idx].size(),
                                prn);
        if (result.ec != std::errc() || prn < 0) {
            return false;
        }
        sat.prn = static_cast<uint8_t>(prn);

        // Parse elevation (optional field)
        if (base_idx + 1 < count && !tokens[base_idx + 1].empty()) {
            int elev = 0;
            result = std::from_chars(tokens[base_idx + 1].data(),
                                    tokens[base_idx + 1].data() + tokens[base_idx + 1].size(),
                                    elev);
            if (result.ec != std::errc() || elev < 0 || elev > 90) {
                return false;
            }
            sat.elevation = static_cast<uint8_t>(elev);
        } else {
            sat.elevation = 0;
        }

        // Parse azimuth (optional field)
        if (base_idx + 2 < count && !tokens[base_idx + 2].empty()) {
            int azim = 0;
            result = std::from_chars(tokens[base_idx + 2].data(),
                                    tokens[base_idx + 2].data() + tokens[base_idx + 2].size(),
                                    azim);
            if (result.ec != std::errc() || azim < 0 || azim > 359) {
                return false;
            }
            sat.azimuth = static_cast<uint16_t>(azim);
        } else {
            sat.azimuth = 0;
        }

        // Parse SNR (optional field)
        if (base_idx + 3 < count && !tokens[base_idx + 3].empty()) {
            int snr = 0;
            result = std::from_chars(tokens[base_idx + 3].data(),
                                    tokens[base_idx + 3].data() + tokens[base_idx + 3].size(),
                                    snr);
            if (result.ec != std::errc() || snr < 0 || snr > 99) {
                return false;
            }
            sat.snr = static_cast<uint8_t>(snr);
        } else {
            sat.snr = 0;
        }

        data.num_satellites++;
    }

    return true;
}



bool Validate(std::string_view sv)
{
    if (sv.size() < 4 || sv.front() != '$') return false;

    size_t star = sv.rfind('*');
    if (star == std::string_view::npos || star + 2 >= sv.size()) return false;

    uint8_t calc = CalcChecksum(sv.substr(1, star - 1));

    auto hex = [](char c)->int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };

    int hi = hex(sv[star+1]);
    int lo = hex(sv[star+2]);
    if (hi < 0 || lo < 0) return false;

    return calc == ((hi << 4) | lo);
}

std::string_view Crop(std::string_view sv)
{
    size_t star = sv.rfind('*');
    if (star <= 1) return {};
    return sv.substr(1, star - 1);
}

uint8_t CalcChecksum(std::string_view sv)
{
    uint8_t cs = 0;
    for (char c : sv)
        cs ^= (uint8_t)c;
    return cs;
}

size_t Tokenize(std::string_view sv, std::string_view* out, size_t max)
{
    if (out == nullptr) {
        return 0;
    }
    size_t n = 0;
    while (n < max) {
        size_t p = sv.find(',');
        std::string_view t = (p == std::string_view::npos) ? sv : sv.substr(0, p);
        out[n++] = Trim(t);
        if (p == std::string_view::npos) break;
        sv.remove_prefix(p + 1);
    }
    return n;
}

size_t TokenizeFull(std::string_view sv, std::string_view* out, size_t max)
{
    if (!Validate(sv)) {
        return 0;
    }

    std::string_view payload = Crop(sv);
    return Tokenize(payload, out, max);
}

bool ParseNmea(std::string_view* tokens, size_t count, NmeaData& data)
{
    // Need at least one token (sentence type)
    if (count < 1 || tokens == nullptr) {
        data.type = NmeaSentenceType::UNKNOWN;
        return false;
    }

    // Identify sentence type
    data.type = IdentifyNmeaSentence(tokens[0]);

    // Parse based on type
    switch (data.type) {
        case NmeaSentenceType::GPGGA:
        case NmeaSentenceType::GNGGA:
        case NmeaSentenceType::GLGGA:
        case NmeaSentenceType::GAGGA:
        case NmeaSentenceType::BDGGA:
            return ParseGGA(tokens, count, data.data.gga, data.type);

        case NmeaSentenceType::GPRMC:
        case NmeaSentenceType::GNRMC:
        case NmeaSentenceType::GLRMC:
        case NmeaSentenceType::GARMC:
        case NmeaSentenceType::BDRMC:
            return ParseRMC(tokens, count, data.data.rmc, data.type);

        case NmeaSentenceType::GPGSV:
        case NmeaSentenceType::GNGSV:
        case NmeaSentenceType::GLGSV:
        case NmeaSentenceType::GAGSV:
        case NmeaSentenceType::GBGSV:
            return ParseGSV(tokens, count, data.data.gsv, data.type);

        case NmeaSentenceType::UNKNOWN:
        default:
            return false;
    }
}


float NmeaCoordToDecimal(float nmea_coord, char direction)
{
    // Convert from ddmm.mmmm (or dddmm.mmmm) to decimal degrees
    int degrees = static_cast<int>(nmea_coord / 100);
    float minutes = nmea_coord - (degrees * 100);
    float decimal = degrees + (minutes / 60.0f);

    // Apply direction (S and W are negative)
    if (direction == 'S' || direction == 'W') {
        decimal = -decimal;
    }

    return decimal;
}

uint32_t NmeaToUtcSeconds(float utc_time, uint32_t date)
{
    // Extract time components
    uint32_t time_int = static_cast<uint32_t>(utc_time);
    uint32_t hours = time_int / 10000;
    uint32_t minutes = (time_int / 100) % 100;
    uint32_t seconds = time_int % 100;

    // Validate time
    if (hours >= 24 || minutes >= 60 || seconds >= 60) {
        return 0;
    }

    // Extract date components from DDMMYY format
    // Date is stored as integer, so 060180 becomes 60180
    // We need to extract digits properly
    uint32_t year = date % 100;           // Last 2 digits: YY
    date /= 100;
    uint32_t month = date % 100;          // Next 2 digits: MM
    date /= 100;
    uint32_t day = date;                  // Remaining digits: DD

    // Validate date
    if (day == 0 || day > 31 || month == 0 || month > 12) {
        return 0;
    }

    // Convert 2-digit year: 00-79 = 2000-2079, 80-99 = 1980-1999
    if (year >= 80) {
        year += 1900;
    } else {
        year += 2000;
    }

    // Calculate days since Unix epoch (01.01.1970)
    // Simple calculation without leap year consideration for brevity
    // For production code, consider using a proper date library
    uint32_t days = 0;

    // Days from years
    for (uint32_t y = 1970; y < year; y++) {
        if ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)) {
            days += 366; // Leap year
        } else {
            days += 365;
        }
    }

    // Days from months
    const uint32_t days_in_month[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    for (uint32_t m = 1; m < month; m++) {
        days += days_in_month[m - 1];
        // Add leap day if February and leap year
        if (m == 2 && ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))) {
            days += 1;
        }
    }

    // Add days in current month
    days += day - 1;

    // Convert to seconds
    uint32_t utc_seconds = days * 86400 + hours * 3600 + minutes * 60 + seconds;

    return utc_seconds;
}

bool NmeaToTm(float utc_time, uint32_t date, std::tm* tm)
{
    if (tm == nullptr) {
        return false;
    }

    // Extract time components
    uint32_t time_int = static_cast<uint32_t>(utc_time);
    uint32_t hours = time_int / 10000;
    uint32_t minutes = (time_int / 100) % 100;
    uint32_t seconds = time_int % 100;

    // Validate time
    if (hours >= 24 || minutes >= 60 || seconds >= 60) {
        return false;
    }

    // Extract date components from DDMMYY format
    // Date is stored as integer, so 060180 becomes 60180
    // We need to extract digits properly
    uint32_t year = date % 100;           // Last 2 digits: YY
    date /= 100;
    uint32_t month = date % 100;          // Next 2 digits: MM
    date /= 100;
    uint32_t day = date;                  // Remaining digits: DD

    // Validate date
    if (day == 0 || day > 31 || month == 0 || month > 12) {
        return false;
    }

    // Convert 2-digit year: 00-79 = 2000-2079, 80-99 = 1980-1999
    if (year >= 80) {
        year += 1900;
    } else {
        year += 2000;
    }

    // Fill tm structure
    tm->tm_sec = seconds;              // Seconds (0-59)
    tm->tm_min = minutes;              // Minutes (0-59)
    tm->tm_hour = hours;               // Hours (0-23)
    tm->tm_mday = day;                 // Day of month (1-31)
    tm->tm_mon = month - 1;            // Month (0-11)
    tm->tm_year = year - 1900;         // Years since 1900 (2000-2099 -> 100-199)
    tm->tm_wday = 0;                   // Day of week (not calculated, use mktime)
    tm->tm_yday = 0;                   // Day of year (not calculated, use mktime)
    tm->tm_isdst = 0;                  // DST flag (UTC has no DST)

    return true;
}

float KnotsToMps(float knots)
{
    return knots * 0.514444f;
}

} // namespace Airoha::Nmea
