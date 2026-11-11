#ifndef __CLIOptions_h__
#define __CLIOptions_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "Porting.h"
#include "ASCIICodes.h"

#define CLI_ATTR_DEF            ASCII_TEXT_WHITE   ASCII_BG_BLACK
#define CLI_ATTR_CMD_HELP       ASCII_TEXT_YELLOW  ASCII_BG_BLACK
#define CLI_ATTR_CMD_UNKNOWN    ASCII_TEXT_MAGENTA ASCII_BG_BLACK
#define CLI_ATTR_MAIN_PROMPT    ASCII_TEXT_CYAN    ASCII_BG_BLACK
#define CLI_ATTR_UTIL_PROMPT    ASCII_TEXT_GREEN   ASCII_BG_BLACK
#define CLI_ATTR_ERROR          ASCII_TEXT_RED     ASCII_BG_BLACK
#define CLI_ATTR_ERROR_2        ASCII_TEXT_WHITE   ASCII_BG_RED
#define CLI_ATTR_SUCCESS        ASCII_TEXT_GREEN   ASCII_BG_BLACK

//#define CLI_UTIL_PROMPT         CLI_ATTR_UTIL_PROMPT ">" CLI_ATTR_DEF
#define CLI_UTIL_PROMPT         ">"

#define CLI_INVALID_PARAM_MSG           "invalid parameter\n"
#define CLI_INVALID_PARAM_NUMBER_MSG    "invalid number of parameters\n"

#define CLI_MANAGER_BUFFER_LEN          (256) // Max input buffer length (for a command and parameters)
#define CLI_REGISTRY_SIZE               (32)  // Max number of registered utils
#define CLI_PARAMS_COUNT                (20)  // Max number of parameters
#define CLI_KEYBOARD_BUFFER             (256) // Max size of the keyboard buffer

#define CLI_CHECK_MEMORY_LEAK           (0)

#define CLI_HISTORY_DEEP                (5)
#define CLI_CMD_EDIT_ENABLE             (1)

#ifdef __cplusplus
 }
#endif

#endif
