/**
 * @file AirohaCommandInternal.hpp
 * @brief Command for internal usage
 */

#pragma once

#include "AG3335/AirohaCommandContext.hpp"
#include "AG3335/AirohaType.hpp"
#include "AG3335/AirohaNmeaParser.hpp"


namespace Airoha::Command
{

// Special internal command
class Internal : public Command::Context
{
public:
    static constexpr std::string_view skCmdId {"int"};

    enum class Id : uint8_t {
        NONE = 0,
        ON,
        OFF,
        NMEA_GGA,
        NMEA_RMC,
        NMEA_GSV,
        PAIR_010,   // PAIR_REQUEST_AIDING
    };

    Id mId = Id::NONE;

    union {
        Nmea::GGA nmeaGGA;
        Nmea::RMC nmeaRMC;
        Nmea::GSV nmeaGSV;
        ReqAiding pair010;
    };

    Internal() { setCmdId(skCmdId); }

};

} // namespace Airoha::Command
