/**
 ******************************************************************************
 * @file    StrCheck.c
 * @brief   This component checks if the specified string contains a float number,
 *          signed or signed integer. Also it can convert the string 
 *          to float and integer data types. 
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "StrCheck/StrCheck.h"

#include <ctype.h>
#include <stdlib.h>

/**
 * @brief Checks whether the string contains a float
 * 
 * @param s   Pointer to the string
 * @param eol End-of-line symbol
 * @return    True if the string contains a float; otherwise false.
 */
bool StrCheck_isFloat(const char* s, char eol)
{
    if (*s == '-' || *s == '+') {
        if (!isdigit((int)*++s)) {
            return 0;
        }
    }

    if (!isdigit((int)*s)) {
        return 0;
    }

    while (isdigit((int)*s)) {
        s++;
    }

    if (*s == '.') {
        if (!isdigit((int)*(++s))) {
            return 0;
        }
    }

    while (isdigit((int)*s)) {
        s++;
    }

    if (*s == 'e' || *s == 'E') {
        s++;
        if (*s == '+' || *s == '-') {
            s++;
            if (!isdigit((int)*s)) {
                return 0;
            }
        }
        else if (!isdigit((int)*s)) {
            return 0;
        }
    }

    while (isdigit((int)*s)) {
        s++;
    }

    return (*s == eol);
}

/**
 * @brief Checks whether the string contains an integer
 * 
 * @param s   Pointer to the string
 * @param eol End-of-line symbol
 * @return    True if the string contains an integer; otherwise false.
 */
bool StrCheck_isInt(const char* s, char eol)
{
    if (*s == '-' || *s == '+') {
        if (!isdigit((int)*++s)) {
            return 0;
        }
    }

    if (!isdigit((int)*s)) {
        return 0;
    }

    while(isdigit((int)*s)) {
        s++;
    }

    return (*s == eol);
}

/**
 * @brief Checks whether the string contains an unsigned integer
 * 
 * @param s   Pointer to the string
 * @param eol End-of-line symbol
 * @return    True if the string contains an unsigned integer; otherwise false.
 */
bool StrCheck_isHex(const char* s, char eol)
{
    if (s == NULL) {
        return false;
    }

    if (*s != '0') {
        return false;
    }

    ++s;

    if (*s != 'x') {
        return false;
    }

    ++s;

    while (isxdigit((int)*s)) {
        ++s;
    }

    return (*s == eol);
}

/**
 * @brief Checks whether the string contains an unsigned integer
 *
 * @param s   Pointer to the string
 * @param eol End-of-line symbol
 * @return    True if the string contains an unsigned integer; otherwise false.
 */
bool StrCheck_isUInt(const char* s, char eol)
{
    if (s == NULL) {
        return false;
    }
    
    if (*s == '+') {
        if (!isdigit((int)*++s)) {
            return 0;
        }
    }

    if (!isdigit((int)*s)) {
        return 0;
    }

    while (isdigit((int)*s)) {
        ++s;
    }

    return (*s == eol);
}

/**
 * @brief Tries to convert a string to a double value
 * 
 * @param s     Pointer to the string
 * @param value Pointer to a variable of type double
 * @return      True if the string contains an unsigned integer; otherwise false.
 */
bool StrCheck_s2double(const char* s, double* value)
{
    if (StrCheck_isFloat(s, '\0') == false) {
        return false;
    }

    *value = atof(s);

    return true;
}

/**
 * @brief Tries to convert a string to a floating point value
 * 
 * @param s     Pointer to the string
 * @param value Pointer to a variable of type float
 * @return      True if the string contains an unsigned integer; otherwise false.
 */
bool StrCheck_s2float(const char* s, float* value)
{
    if (StrCheck_isFloat(s, '\0') == false) {
        return false;
    }

    *value = (float)atof(s);

    return true;
}

/**
 * @brief Tries to convert a HEX string to a uint32_t value
 *
 * @param s     Pointer to the HEX string (0xAAAAAAA format)
 * @param value Pointer to a variable of type uint32_t
 * @return      True if successfully; otherwise false.
 */
bool StrCheck_hex2ui32(const char* s, uint32_t* value)
{
    if (s == NULL) {
        return false;
    }

    if (value == NULL) {
        return false;
    }

    *value = (uint32_t) strtol(s, (char**)0, 0);

    return true;
}

/**
 * @brief Tries to convert a string to a uint32_t value
 * 
 * @param s     Pointer to the string
 * @param value Pointer to a variable of type uint32_t
 * @return      True if successfully; otherwise false.
 */
bool StrCheck_s2ui32(const char* s, uint32_t* value)
{
    if (s == NULL) {
        return false;
    }

    if (value == NULL) {
        return false;
    }
    
    if ((!StrCheck_isUInt(s, '\0')) && (!StrCheck_isHex(s, '\0'))) {
        return false;
    }

    *value = (uint32_t) strtol(s, (char**)0, 0);

    return true;
}

/**
 * @brief Tries to convert a string to a int32_t value
 * 
 * @param s     Pointer to the string
 * @param value Pointer to a variable of type int32_t
 * @return      True if successfully; otherwise false.
 */
bool StrCheck_s2i32(const char* s, int32_t* value)
{
    if (s == NULL) {
        return false;
    }

    if (value == NULL) {
        return false;
    }

    if (StrCheck_isInt(s, '\0') == false) {
        return false;
    }

    *value = atol(s);

    return true;
}

/**
 * @brief Tries to convert a string to a uint16_t value
 * 
 * @param s     Pointer to the string
 * @param value Pointer to a variable of type uint16_t
 * @return      True if successfully; otherwise false.
 */
bool StrCheck_s2ui16(const char* s, uint16_t* value)
{
    uint32_t tmpValue;
    if (StrCheck_s2ui32(s, &tmpValue) == false) {
        return false;
    }

    if (tmpValue > 65535) {
        return false;
    }

    *value = (uint16_t) tmpValue;

    return true;
}

/**
 * @brief Tries to convert a string to a int16_t value
 * 
 * @param s     Pointer to the string
 * @param value Pointer to a variable of type int16_t
 * @return      True if successfully; otherwise false.
 */
bool StrCheck_s2i16(const char* s, int16_t* value)
{
    int32_t tmpValue;
    if (StrCheck_s2i32(s, &tmpValue) == false) {
        return false;
    }

    if ((tmpValue < -32768) || (tmpValue > 32767)) {
        return false;
    }

    *value = (int16_t) tmpValue;

    return true;
}

/**
 * @brief Tries to convert a string to a uint8_t value
 * 
 * @param s     Pointer to the string
 * @param value Pointer to a variable of type uint8_t
 * @return      True if successfully; otherwise false.
 */
bool StrCheck_s2ui8(const char* s, uint8_t* value)
{
    uint32_t tmpValue;
    if (StrCheck_s2ui32(s, &tmpValue) == false) {
        return false;
    }

    if (tmpValue > 255) {
        return false;
    }

    *value = (uint8_t) tmpValue;

    return true;
}

/**
 * @brief Tries to convert a string to a int8_t value
 * 
 * @param s     Pointer to the string
 * @param value Pointer to a variable of type int8_t
 * @return      True if successfully; otherwise false.
 */
bool StrCheck_s2i8(const char* s, int8_t* value)
{
    int32_t tmpValue;
    if (StrCheck_s2i32(s, &tmpValue) == false) {
        return false;
    }

    if ((tmpValue < -128) || (tmpValue > 127)) {
        return false;
    }

    *value = (int8_t) tmpValue;

    return true;
}
