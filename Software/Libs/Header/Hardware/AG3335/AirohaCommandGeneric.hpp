/**
 * @file AirohaCommandGeneric.hpp
 * @brief Generic commands
 */

#pragma once

#include "AG3335/AirohaCommandContext.hpp"

#include <cstdio>
#include <charconv>

namespace Airoha::Command
{


// Generic (No parameters, no response)
class Generic : public Context
{
public:
    Generic() = default;
    void setCmdId(std::string_view cmdId) { Context::setCmdId(cmdId); }
};


// Generic (parameter: bool, no response)
class GenericSetBOOL : public Generic
{
public:
    GenericSetBOOL() = default;
    void setValue(bool v) { value = v; }

private:
    bool value = false;
    virtual size_t formatCommandPayload(char *buffer, size_t size) override
    {
        return snprintf(buffer, size, "%u", value ? 1 : 0);
    }
};

// Generic (No parameters, response: bool)
class GenericGetBOOL : public Generic
{
public:
    GenericGetBOOL() = default;
    bool getValue() { return value; }

private:
    bool value = false;
    virtual bool parseResponsePayload(const std::string_view* tokens, size_t count) override
    {
        return parseBOOL(tokens, count, value);
    }

    bool parseBOOL(const std::string_view* tokens, size_t count, bool &data)
    {
        // Expected: PAIRxxx, status (2 tokens)
        if (count < 2 || tokens[1].size() > 1) {
            return false;
        }
        // Token 0 is "PAIRxxx" - already verified by caller

        // Validate status (token 1)
        char mode_char = tokens[1][0];
        switch (mode_char) {
            case '0': data = false; break;
            case '1': data = true; break;
            default: return false;
        }
        return true;
    }
};


// Generic (parameter: uint32_t, no response)
class GenericSetUINT : public Generic
{
public:
    GenericSetUINT() = default;
    void setValue(uint32_t v) { value = v; }

private:
    uint32_t value = 0;
    virtual size_t formatCommandPayload(char *buffer, size_t size) override
    {
        return snprintf(buffer, size, "%lu", value);
    }
};

// Generic (No parameters, response: uint32_t)
class GenericGetUINT : public Generic
{
public:
    GenericGetUINT() = default;
    uint32_t getValue() { return value; }

private:
    uint32_t value = 0;
    virtual bool parseResponsePayload(const std::string_view* tokens, size_t count) override
    {
        return parseUINT(tokens, count, value);
    }

    bool parseUINT(const std::string_view* tokens, size_t count, uint32_t &data)
    {
        // Expected: PAIRxxx, status (2 tokens)
        if (count < 2 || tokens[1].size() > 1) {
            return false;
        }
        // Token 0 is "PAIRxxx" - already verified by caller

        // Parse Type (token 1)
        auto result = std::from_chars(tokens[2].data(),
                                      tokens[2].data() + tokens[2].size(),
                                      data);
        if (result.ec != std::errc()) {
            return false;
        }

        return true;
    }
};


// Generic (parameter: int32_t, no response)
class GenericSetINT : public Generic
{
public:
    GenericSetINT() = default;
    void setValue(int32_t v) { value = v; }

private:
    int32_t value = 0;
    virtual size_t formatCommandPayload(char *buffer, size_t size) override
    {
        return snprintf(buffer, size, "%ld", value);
    }
};

// Generic (No parameters, response: int32_t)
class GenericGetINT : public Generic
{
public:
    GenericGetINT() = default;
    int32_t getValue() { return value; }

private:
    int32_t value = 0;
    virtual bool parseResponsePayload(const std::string_view* tokens, size_t count) override
    {
        return parseINT(tokens, count, value);
    }

    bool parseINT(const std::string_view* tokens, size_t count, int32_t &data)
    {
        // Expected: PAIRxxx, status (2 tokens)
        if (count < 2 || tokens[1].size() > 1) {
            return false;
        }
        // Token 0 is "PAIRxxx" - already verified by caller

        // Parse Type (token 1)
        auto result = std::from_chars(tokens[2].data(),
                                      tokens[2].data() + tokens[2].size(),
                                      data);
        if (result.ec != std::errc()) {
            return false;
        }

        return true;
    }
};


// Generic (parameter: float, no response)
class GenericSetFLOAT : public Generic
{
public:
    GenericSetFLOAT() = default;
    void setValue(float v, int p = 1) { value = v; precision = p; }

private:
    float value = 0.0f;
    int precision = 0;
    virtual size_t formatCommandPayload(char *buffer, size_t size) override
    {
        return snprintf(buffer, size, "%.*f", precision, value);
    }
};

// Generic (No parameters, response: float)
class GenericGetFLOAT : public Generic
{
public:
    GenericGetFLOAT() = default;
    float getValue() { return value; }

private:
    float value = 0.0;
    virtual bool parseResponsePayload(const std::string_view* tokens, size_t count) override
    {
        return parseFLOAT(tokens, count, value);
    }

    bool parseFLOAT(const std::string_view* tokens, size_t count, float &data)
    {
        // Expected: PAIRxxx, status (2 tokens)
        if (count < 2 || tokens[1].size() > 1) {
            return false;
        }
        // Token 0 is "PAIRxxx" - already verified by caller

        // Parse Type (token 1)
        auto result = std::from_chars(tokens[2].data(),
                                      tokens[2].data() + tokens[2].size(),
                                      data);
        if (result.ec != std::errc()) {
            return false;
        }

        return true;
    }
};


} // namespace Airoha::Command
