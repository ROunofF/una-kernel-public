/**
 * @file AirohaCmd.hpp
 * @brief Command for internal usage
 */

#pragma once

#include "AG3335/AirohaCommandContext.hpp"
#include "AG3335/AirohaCommandGeneric.hpp"
#include "AG3335/AirohaType.hpp"

#include "AG3335/AirohaNmeaParser.hpp"

#include <cstdio>
#include <charconv>

namespace Airoha::Command
{

// PAIR024
class GetChipVersion : public Context
{
public:
    char version = '\0';
    GetChipVersion() { setCmdId("024"); }

    virtual bool parseResponsePayload(const std::string_view* tokens,
            size_t count) override
    {
        return parseChipVersion(tokens, count, version);
    }

private:
    bool parseChipVersion(const std::string_view* tokens, size_t count, char &data)
    {
        // Expected: PAIR024, status (2 tokens)
        if (count < 2 || tokens[1].size() > 1) {
            return false;
        }

        // Token 0 is "PAIR024" - already verified by caller

        // Validate status (token 1)
        data = tokens[1][0];

        return true;
    }
};


// PAIR080
class SetNavigationMode : public Context
{
public:
    SetNavigationMode() { setCmdId("080"); }
    void setGpsNavMode(GpsNavMode v) { value = v; }

private:
    GpsNavMode value {};
    virtual size_t formatCommandPayload(char *buffer, size_t size) override
    {
        return snprintf(buffer, size, "%u", static_cast<uint8_t>(value));
    }
};

// PAIR081
class GetNavigationMode : public Context
{
public:
    GetNavigationMode() { setCmdId("081"); }
    GpsNavMode getGpsNavMode() { return value; }

private:
    GpsNavMode value = {};
    virtual bool parseResponsePayload(const std::string_view* tokens,
            size_t count) override
    {
        return parseNavigationMode(tokens, count, value);
    }

    bool parseNavigationMode(const std::string_view* tokens, size_t count, GpsNavMode &data)
    {
        if (count < 2) {
            return false;
        }

        char mode_char = tokens[1][0];
        switch (mode_char) {
            case '0': data = GpsNavMode::NORMAL; break;
            case '1': data = GpsNavMode::FITNESS; break;
            case '3': data = GpsNavMode::BALLON; break;
            case '4': data = GpsNavMode::STATIONARY; break;
            case '5': data = GpsNavMode::DRONE; break;
            case '7': data = GpsNavMode::SWIMMING; break;
            case '8': data = GpsNavMode::BIKE; break;
            default: return false;
        }
        return true;
    }
};


// PAIR086
class SetDebugLogOutput : public Context
{
public:
    SetDebugLogOutput() { setCmdId("086"); }
    void setDebugLog(DebugLog v) { value = v; }

private:
    DebugLog value {};
    virtual size_t formatCommandPayload(char *buffer, size_t size) override
    {
        return snprintf(buffer, size, "%u", static_cast<uint8_t>(value));
    }
};

// PAIR087
class GetDebugLogOutput : public Context
{
public:
    GetDebugLogOutput() { setCmdId("087"); }
    DebugLog getDebugLog() { return value; }

private:
    DebugLog value = {};
    virtual bool parseResponsePayload(const std::string_view* tokens, size_t count) override
    {
        return parseNavigationMode(tokens, count, value);
    }

    bool parseNavigationMode(const std::string_view* tokens, size_t count, DebugLog &data)
    {
        if (count < 2) {
            return false;
        }

        char mode_char = tokens[1][0];
        switch (mode_char) {
            case '0': data = DebugLog::DISABLE; break;
            case '1': data = DebugLog::ENABLEFULL; break;
            case '3': data = DebugLog::ENABLELITE; break;
            default: return false;
        }
        return true;
    }
};


// PAIR165
class GetOdometerStatus : public Context
{
public:
    GetOdometerStatus() { setCmdId("165"); }
    bool isEnabled() { return status; }
    float getDistanse() { return value; }

private:
    bool status = false;
    float value = 0.0f;
    virtual bool parseResponsePayload(const std::string_view* tokens, size_t count) override
    {
        return parseOdometerStatus(tokens, count, status, value);
    }

    bool parseOdometerStatus(const std::string_view* tokens, size_t count, bool &en, float &value)
    {
        if (count < 3) {
            return false;
        }

        char mode_char = tokens[1][0];
        switch (mode_char) {
            case '0': en = false; break;
            case '1': en = true; break;
            default: return false;
        }

        // Parse Type (token 1)
        float type_val = -1;
        auto result = std::from_chars(tokens[2].data(),
                                      tokens[2].data() + tokens[2].size(),
                                      type_val);
        if (result.ec != std::errc() || type_val < 0) {
            return false;
        }

        value = type_val;
        return true;
    }
};


// PAIR470
class GetEpoStatus : public Context
{
public:
    GetEpoStatus() { setCmdId("470"); }
    EpoGnssStatus getEpoStatus() { return status; }

private:
    EpoGnssStatus status {};
    virtual bool parseResponsePayload(const std::string_view* tokens, size_t count) override
    {
        return parseEpoStatus(tokens, count, status);
    }

    bool parseEpoStatus(const std::string_view* tokens, size_t count, EpoGnssStatus &data)
    {
        if (count < 11) {
            return false;
        }

        char mode_char = tokens[1][0];
        switch (mode_char) {
            case '0': data.SysId = SystemType::GPS; break;
            case '1': data.SysId = SystemType::GLONASS; break;
            case '2': data.SysId = SystemType::GALILEO; break;
            case '3': data.SysId = SystemType::BEIDOU; break;
            case '4': data.SysId = SystemType::QZSS; break;
            default: return false;
        }

        // Parse Type (token 1)
        uint32_t type_val = 0;
        auto result = std::from_chars(tokens[2].data(),
                                      tokens[2].data() + tokens[2].size(),
                                      type_val);
        if (result.ec != std::errc()) {
            return false;
        }

        data.Set = type_val;

        type_val = 0;
        result = std::from_chars(tokens[3].data(),
                                      tokens[3].data() + tokens[3].size(),
                                      type_val);
        if (result.ec != std::errc()) {
            return false;
        }

        data.FWN = type_val;

        type_val = 0;
        result = std::from_chars(tokens[4].data(),
                                      tokens[4].data() + tokens[4].size(),
                                      type_val);
        if (result.ec != std::errc()) {
            return false;
        }

        data.FTOW = type_val;

        type_val = 0;
        result = std::from_chars(tokens[5].data(),
                                      tokens[5].data() + tokens[5].size(),
                                      type_val);
        if (result.ec != std::errc()) {
            return false;
        }

        data.LWN = type_val;

        type_val = 0;
        result = std::from_chars(tokens[6].data(),
                                      tokens[6].data() + tokens[6].size(),
                                      type_val);
        if (result.ec != std::errc()) {
            return false;
        }

        data.LTOW = type_val;

        type_val = 0;
        result = std::from_chars(tokens[7].data(),
                                      tokens[7].data() + tokens[7].size(),
                                      type_val);
        if (result.ec != std::errc()) {
            return false;
        }

        data.FCWN = type_val;

        type_val = 0;
        result = std::from_chars(tokens[8].data(),
                                      tokens[8].data() + tokens[8].size(),
                                      type_val);
        if (result.ec != std::errc()) {
            return false;
        }

        data.FCTOW = type_val;

        type_val = 0;
        result = std::from_chars(tokens[9].data(),
                                      tokens[9].data() + tokens[9].size(),
                                      type_val);
        if (result.ec != std::errc()) {
            return false;
        }

        data.LCWN = type_val;

        type_val = 0;
        result = std::from_chars(tokens[10].data(),
                                      tokens[10].data() + tokens[10].size(),
                                      type_val);
        if (result.ec != std::errc()) {
            return false;
        }

        data.LCTOW = type_val;

        return true;
    }
};

} // namespace Airoha::Command
