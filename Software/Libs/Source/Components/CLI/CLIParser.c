/**
 ******************************************************************************
 * @file    CLIConsole.c
 * @brief   This component provides the ability to work with a set of parameters
 *          for a command that was received and parsed by the CLIInput component.
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "CLI/CLIParser.h"
#include "StrCheck/StrCheck.h"

#include <string.h>
#include <assert.h>

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initializes parser and put argc/argv values
 * 
 * @param p    Pointer to CLIParser_t object
 * @param argc Number of arguments including command name in argv list
 * @param argv List of arguments including command name
 */
void CLIParser_init(CLIParser_t* p, int argc, const char** argv)
{
    assert(p != NULL);

    p->argC = argc;
    p->argV = argv;
}

/**
 * @brief Checks if the specified parameter is in the argv list
 * 
 * @param p       Pointer to CLIParser_t object
 * @param optName Option name
 * @return        True if the specified option name is present in the argv list, false otherwise.
 */
bool CLIParser_check(CLIParser_t* p, const char* optName)
{
    for (int idx = 0; idx < p->argC; ++idx) {
        if (strcmp(p->argV[idx], optName) == 0) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Checks if specified option has value
 * 
 * @param p       Pointer to CLIParser_t object
 * @param optName Option name
 * @return        True if the specified option has value, otherwise false.
 */
bool CLIParser_valueExists(CLIParser_t* p, const char* optName)
{
    return (CLIParser_getValueAsString(p, optName) != NULL);
}

/**
 * @brief Returns pointer to the option value
 * 
 * @param p            Pointer to CLIParser_t object
 * @param optName      Option name
 * @return const char* Pointer to the option value
 */
const char* CLIParser_getValueAsString(CLIParser_t* p, const char* optName)
{
    for (int idx = 0; idx < p->argC - 1; ++idx) {
        if (strcmp(p->argV[idx], optName) == 0) {
            return p->argV[idx + 1];
        }
    }

    return NULL;
}

/**
 * @brief Returns the value of the specified option name as uint32_t type
 * 
 * @param p       Pointer to CLIParser_t object
 * @param optName Option name
 * @param value   The value of the specified option name
 * @return        True if option value was converted successfully otherwise false.
 */
bool CLIParser_getValueAsUI32(CLIParser_t* p, const char* optName, uint32_t* value)
{
    return StrCheck_s2ui32(CLIParser_getValueAsString(p, optName), value);
}

/**
 * @brief Returns the value of the specified option name as int32_t type
 * 
 * @param p       Pointer to CLIParser_t object
 * @param optName Option name
 * @param value   The value of the specified option name
 * @return        True if option value was converted successfully otherwise false.
 */
bool CLIParser_getValueAsI32(CLIParser_t* p, const char* optName, int32_t* value)
{
    return StrCheck_s2i32(CLIParser_getValueAsString(p, optName), value);
}

/**
 * @brief Returns the value of the specified option name as float type
 *
 * @param p       Pointer to CLIParser_t object
 * @param optName Option name
 * @param value   The value of the specified option name
 * @return        True if option value was converted successfully otherwise false.
 */
bool CLIParser_getValueAsFloat(CLIParser_t* p, const char* optName, float* value)
{
    return StrCheck_s2float(CLIParser_getValueAsString(p, optName), value);
}

/**
 * @brief Returns count of elements in the array
 *
 * @param p       Pointer to CLIParser_t object
 * @param optName Array name
 * @return        Count of elements
 */
uint8_t CLIParser_getElements(CLIParser_t* p, const char* optName)
{
    for (int idx = 0; idx < p->argC; ++idx) {
        if (strcmp(p->argV[idx], optName) == 0) {
            return p->argC - 1 - idx;
        }
    }

    return 0;
}

/**
 * @brief Reads an element value as a string
 *
 * @param p       Pointer to CLIParser_t object
 * @param optName Array name
 * @param i       Index of the element
 * @return        Pointer to the element value
 */
const char* CLIParser_getElementAsString(CLIParser_t* p, const char* optName, uint8_t i)
{
    for (int idx = 0; idx < p->argC - 1; ++idx) {
        if (strcmp(p->argV[idx], optName) == 0) {
            int pos = idx + i + 1;
            if (pos < p->argC) {
                return p->argV[pos];
            } else {
                break;
            }
        }
    }

    return NULL;
}

/**
 * @brief Reads an element value as UI32
 *
 * @param p       Pointer to CLIParser_t object
 * @param optName Array name
 * @param i       Index of the element
 * @param value   The value of the specified option name
 * @return        True if value was converted successfully otherwise false.
 */
bool CLIParser_getElementAsUI32(CLIParser_t* p, const char* optName, uint8_t i, uint32_t* value)
{
    return StrCheck_s2ui32(CLIParser_getElementAsString(p, optName, i), value);
}

/**
 * @brief Reads an element value as I32
 *
 * @param p       Pointer to CLIParser_t object
 * @param optName Array name
 * @param i       Index of the element
 * @param value   The value of the specified option name
 * @return        True if value was converted successfully otherwise false.
 */
bool CLIParser_getElementAsI32(CLIParser_t* p, const char* optName, uint8_t i, int32_t* value)
{
    return StrCheck_s2i32(CLIParser_getElementAsString(p, optName, i), value);
}

/**
 * @brief Reads an element value as Float
 *
 * @param p       Pointer to CLIParser_t object
 * @param optName Array name
 * @param i       Index of the element
 * @param value   The value of the specified option name
 * @return        True if value was converted successfully otherwise false.
 */
bool CLIParser_getElementAsFloat(CLIParser_t* p, const char* optName, uint8_t i, float* value)
{
    return StrCheck_s2float(CLIParser_getElementAsString(p, optName, i), value);
}
