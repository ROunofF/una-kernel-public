/**
 ******************************************************************************
 * @file    StrCheck.h
 * @brief   Header file for StrCheck component.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __StrCheck_h__
#define __StrCheck_h__

#include "Porting.h"

#ifdef __cplusplus
 extern "C" {
#endif

bool StrCheck_isFloat(const char *s, char eol); // eol = '\0'
bool StrCheck_isInt(const char   *s, char eol); // eol = '\0'
bool StrCheck_isUInt(const char  *s, char eol); // eol = '\0'
bool StrCheck_isHex(const char  *s, char eol); // eol = '\0'

bool StrCheck_s2double(const char* s, double* value);
bool StrCheck_s2float(const char* s, float* value);
bool StrCheck_hex2ui32(const char* s, uint32_t* value);
bool StrCheck_s2ui32(const char* s, uint32_t* value);
bool StrCheck_s2i32(const char* s, int32_t* value);
bool StrCheck_s2ui16(const char* s, uint16_t* value);
bool StrCheck_s2i16(const char* s, int16_t* value);
bool StrCheck_s2ui8(const char* s, uint8_t* value);
bool StrCheck_s2i8(const char* s, int8_t* value);

#ifdef __cplusplus
 }
#endif

#endif
