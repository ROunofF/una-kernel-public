/**
 ******************************************************************************
 * @file    NmeaParser.hpp
 * @brief   NMEA parser.
 ******************************************************************************
 *
 ******************************************************************************
 */

#pragma once


#include <string_view>
#include <cstdint>
#include <ctime>

namespace Airoha::Nmea
{

/**
 * @brief GNSS system type
 */
enum class GnssSystem {
    UNKNOWN = 0,  // Unknown system

    GPS,          // GPS (USA)
    GLONASS,      // GLONASS (Russia)
    GALILEO,      // Galileo (EU)
    BEIDOU,       // BeiDou (China)
    MULTI         // Multi-GNSS combined
};

/**
 * @brief NMEA sentence types
 */
enum class NmeaSentenceType {
    UNKNOWN = 0,  // Unknown or unsupported sentence type

    GPGGA,        // Global Positioning System Fix Data
    GPRMC,        // Recommended Minimum Specific GNSS Data
    GPGSV,        // GNSS Satellites in View
    GPGLL,        // Geographic Position - Latitude/Longitude
    GPGSA,        // GNSS DOP and Active Satellites
    GPVTG,        // Course Over Ground and Ground Speed

    GNGGA,        // Multi-GNSS Fix Data
    GNRMC,        // Multi-GNSS Recommended Minimum
    GNGSV,        // Multi-GNSS Satellites in View

    GLGGA,        // GLONASS Fix Data
    GLRMC,        // GLONASS Recommended Minimum
    GLGSV,        // GLONASS Satellites in View

    GAGGA,        // Galileo Fix Data
    GARMC,        // Galileo Recommended Minimum
    GAGSV,        // Galileo Satellites in View

    BDGGA,        // BeiDou Fix Data
    BDRMC,        // BeiDou Recommended Minimum
    GBGSV         // BeiDou Satellites in View
};


/**
 * @brief GPGGA - Global Positioning System Fix Data
 */
struct GGA {
    enum FixQuality {
        INVALID         = 0, // Fix not available
        GPS_FIX         = 1, // GPS fix
        DGPS_FIX        = 2, // Differential GPS fix
        PPS_FIX         = 3, // PPS fix
        RTK_FIX         = 4, // Real Time Kinematic
        RTK_FLOAT       = 5, // Float RTK
        ESTIMATED       = 6, // Estimated (dead reckoning)
        MANUAL_INPUT    = 7, // Manual input mode
        SIMULATION      = 8  // Simulation mode
    };

    GnssSystem system;     // GNSS system type
    float utc_time;        // UTC time (hhmmss.sss)
    float latitude;        // Latitude in decimal degrees
    float longitude;       // Longitude in decimal degrees
    FixQuality fix_quality;
    uint8_t num_satellites; // Number of satellites in use
    float hdop;            // Horizontal dilution of precision
    float altitude;        // Altitude above mean sea level (meters)
    float geoid_sep;       // Geoid separation (meters)
};

/**
 * @brief GPRMC - Minimum Specific GNSS Data
 */
struct RMC {
    enum Status {
        INVALID = 0, // Data invalid (V)
        VALID   = 1  // Data valid (A)
    };

    enum Mode {
        AUTONOMOUS  = 'A', // Autonomous mode
        DIFFERENTIAL = 'D', // Differential mode
        ESTIMATED   = 'E', // Estimated (dead reckoning) mode
        MANUAL      = 'M', // Manual input mode
        SIMULATED   = 'S', // Simulator mode
        NOT_VALID   = 'N', // Data not valid
        PRECISE     = 'P'  // Precise (no degradation)
    };

    GnssSystem system;     // GNSS system type
    float utc_time;        // UTC time (hhmmss.sss)
    Status status;         // Status A=active, V=void
    float latitude;        // Latitude in decimal degrees
    float longitude;       // Longitude in decimal degrees
    float speed_knots;     // Speed over ground in knots
    float track_angle;     // Track angle in degrees (true)
    uint32_t date;         // Date (ddmmyy)
    float mag_variation;   // Magnetic variation in degrees
    Mode mode;             // Mode indicator (optional)
};


/**
 * @brief Satellite information for GSV sentence
 */
struct SatelliteInfo {
    uint8_t prn;           // Satellite PRN (Pseudo-Random Noise) number
    uint8_t elevation;     // Elevation in degrees (0-90)
    uint16_t azimuth;      // Azimuth in degrees (0-359)
    uint8_t snr;           // Signal-to-Noise Ratio in dB (0-99), 0 = not tracked
};

/**
 * @brief GPGSV - GNSS Satellites in View
 *
 * GSV sentences report information about satellites in view.
 * Multiple GSV sentences may be needed to report all satellites.
 *
 * Format: $GPGSV,total_msgs,msg_num,total_sats,sat1_prn,sat1_elev,sat1_azim,sat1_snr,...*CS
 *
 * Example:
 * $GPGSV,3,1,11,03,03,111,00,04,15,270,00,06,01,010,00,13,06,292,00*74
 * $GPGSV,3,2,11,14,25,170,00,16,57,208,39,18,67,296,40,19,40,246,00*74
 * $GPGSV,3,3,11,22,42,067,42,24,14,311,43,27,05,244,00,,,,*4D
 */
struct GSV {
    GnssSystem system;         // GNSS system type
    uint8_t total_messages;    // Total number of GSV messages (1-9)
    uint8_t message_number;    // Current message number (1-9)
    uint8_t total_satellites;  // Total number of satellites in view (0-99)
    uint8_t num_satellites;    // Number of satellites in this message (0-4)
    SatelliteInfo satellites[4]; // Up to 4 satellites per message
};


/**
 * @brief NMEA sentence data container with type discriminator
 */
struct NmeaData {
    NmeaSentenceType type;

    union {
        GGA gga;
        RMC rmc;
        GSV gsv;
    } data;

    // Constructor to initialize type
    NmeaData() : type(NmeaSentenceType::UNKNOWN) {}
};


/**
 * @brief Validates a complete NMEA sentence including '$' prefix and checksum '*XX'.
 * @param sv Full input string view.
 * @return true if checksum and format are valid, false otherwise.
 */
bool Validate(std::string_view sv);

/**
 * @brief Extracts the core payload of an NMEA sentence (removes '$' and '*XX').
 * @param sv Full input string view.
 * @return Payload view, empty if format is invalid.
 */
std::string_view Crop(std::string_view sv);

/**
 * @brief Calculates the NMEA checksum (XOR of all characters in the payload).
 * @param sv Input payload (without '$' and without '*XX').
 * @return Checksum value (0–255).
 */
uint8_t CalcChecksum(std::string_view sv);

/**
 * @brief Splits a payload into comma-separated tokens.
 *
 * Empty tokens are preserved and Trimmed. No memory is allocated, the caller
 * must supply an output array.
 *
 * @param sv Payload string view (without '$' and checksum).
 * @param out Output array of string_view tokens.
 * @param max Maximum number of tokens to store.
 * @return Number of tokens extracted.
 */
size_t Tokenize(std::string_view sv, std::string_view* out, size_t max);

/**
 * @brief Tokenizes a full NMEA sentence including '$' and '*XX'.
 *
 * Performs validation and crops the payload automatically.
 *
 * @param sv Full NMEA string view.
 * @param out Output array of string_view tokens.
 * @param max Maximum number of tokens to store.
 * @return Number of tokens extracted, 0 if sentence is invalid.
 */
size_t TokenizeFull(std::string_view sv, std::string_view* out, size_t max);

/**
 * @brief Parse NMEA sentence from tokenized string
 * @param tokens Array of string_view tokens (comma-separated fields)
 * @param count Number of tokens in the array
 * @param data Output NMEA data structure to fill
 * @return true if parsing successful, false otherwise
 *
 * @note The function automatically identifies the sentence type from tokens[0]
 *       and calls the appropriate parser. The data.type field indicates which
 *       union member contains valid data.
 *
 * Example:
 *   std::string_view tokens[] = {"GPGGA", "123519", "4807.038", "N", ...};
 *   NmeaData nmea_data;
 *   if (ParseNmea(tokens, 15, nmea_data)) {
 *       if (nmea_data.type == NmeaSentenceType::GPGGA) {
 *           float lat = nmea_data.data.gga.latitude;
 *           float lon = nmea_data.data.gga.longitude;
 *       }
 *   }
 */
bool ParseNmea(std::string_view* tokens, size_t count, NmeaData& data);

/**
 * @brief Convert NMEA coordinate format to decimal degrees
 * @param nmea_coord NMEA coordinate (ddmm.mmmm or dddmm.mmmm)
 * @param direction Direction indicator ('N'/'S' for latitude, 'E'/'W' for longitude)
 * @return Coordinate in decimal degrees
 */
float NmeaCoordToDecimal(float nmea_coord, char direction);

/**
 * @brief Convert NMEA time and date to UTC seconds since Unix epoch
 * @param utc_time UTC time in hhmmss.sss format
 * @param date Date in ddmmyy format
 * @return UTC seconds since Unix epoch (01.01.1970), or 0 if invalid
 */
uint32_t NmeaToUtcSeconds(float utc_time, uint32_t date);

/**
 * @brief Convert NMEA time and date to std::tm structure
 * @param utc_time UTC time in hhmmss.sss format
 * @param date Date in ddmmyy format
 * @param tm Pointer to tm structure to fill
 * @return true if conversion successful, false otherwise
 *
 * @note tm_wday (day of week) and tm_yday (day of year) are set to 0.
 *       To calculate these fields, call std::mktime() after this function:
 *
 *       Example:
 *       std::tm time_struct;
 *       if (NmeaToTm(rmc_data.utc_time, rmc_data.date, &time_struct)) {
 *           std::mktime(&time_struct);  // This will fill tm_wday and tm_yday
 *           // Now tm_wday contains day of week (0=Sunday, 6=Saturday)
 *           // And tm_yday contains day of year (0-365)
 *       }
 */
bool NmeaToTm(float utc_time, uint32_t date, std::tm* tm);

/**
 * @brief Convert speed from knots to meters per second
 * @param knots Speed in knots (nautical miles per hour)
 * @return Speed in meters per second (m/s)
 *
 * @note Conversion factor: 1 knot = 1.852 km/h = 0.514444 m/s
 *
 * Example:
 *   float speed_knots = 10.0f;  // 10 knots
 *   float speed_ms = KnotsToMps(speed_knots);  // 5.144 m/s
 */
float KnotsToMps(float knots);

} // namespace Airoha::Nmea
