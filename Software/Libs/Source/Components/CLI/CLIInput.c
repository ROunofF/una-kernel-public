/**
 ******************************************************************************
 * @file    CLIConsole.c
 * @brief   This component receives data from CLIKeyboard, collect them to 
 *          command and pushes to CLIHistory. Also, it handles pressing on 
 *          arrow keys, turn on/of insert mode and editing current command
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "CLI/CLIInput.h"
#include "CLI/CLIConsole.h"
#include "CLI/CLIKeyboard.h"

#include <string.h>
#include <stdio.h>

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

static int CLIInput_isSpace(int c) {
    return c == ' ' || c == '\f' || c == '\n' || c == '\r' || c == '\t' || c == '\v';
}

static int CLIInput_tokenize(char* str, char** argv, int max_args)
{
    int   argc = 0;
    char* end  = NULL;
    char* p    = str;
    int   len  = strlen(str);

    while ((*p) && (argc < max_args) && (end - str < len)) {
        // skip leading white space
        while (*p && CLIInput_isSpace(*p)) {
            p++;
        }

        if (!*p) {
            break;  // end of string
        }

        if (*p == '"' || *p == '\'') {
            char quote = *p;
            // argument is quoted
            end = ++p;
            while (*end && *end != quote) {
                end++;
            }

            if (*end == '\0') {
                return argc;
            }
        } else {
            end = p;
            // argument is unquoted
            while (*end && !CLIInput_isSpace(*end)) {
                end++;
            }
        }

        // store pointer to argument in the argv array
        *end++       = '\0';
        argv[argc++] = p;

        p = end;
    }

    return argc;
}

static bool CLIInput_parsing(CLIInput_t* in)
{
    in->argC = CLIInput_tokenize(in->buffer, in->argV, CLI_PARAMS_COUNT);

    return (in->argC != 0);
}

static void CLIInput_handleSymbolInsMode(CLIInput_t* in, char symbol)
{
    if (in->bufferInsPos == in->bufferEndPos) {
        in->buffer[in->bufferEndPos] = symbol;
    } else {
        CLIConsole_print("\033[1@");

        uint16_t count = in->bufferEndPos - in->bufferInsPos;

        for (uint16_t idx = 0; idx < count; ++idx) {
            in->buffer[in->bufferEndPos - idx] = in->buffer[in->bufferEndPos - idx - 1];
        }

        in->buffer[in->bufferInsPos] = symbol;
    }

    ++in->bufferInsPos;
    ++in->bufferEndPos;
}

#if (CLI_HISTORY_DEEP != 0)
static void CLIInput_clearCurrCommand(CLIInput_t* in)
{
    while (in->bufferEndPos != 0) {
        --in->bufferEndPos;
        
        CLIConsole_putchar(CLI_ASCII_BACKSPACE);
        CLIConsole_putchar(' ');
        CLIConsole_putchar(CLI_ASCII_BACKSPACE);
    }

    CLIInput_reset(in);
}

static void CLIInput_printNewCommand(CLIInput_t* in, const char* cmd)
{
    strcpy(in->buffer, cmd);
    in->bufferInsPos = in->bufferEndPos = strlen(cmd);
    CLIConsole_print(cmd);
}

static void CLIInput_scrollHistoryUp(CLIInput_t* in)
{
    if (CLIHistory_getIndex(&in->history) < 0) {
        CLIHistory_incIndex(&in->history);
    }
        
    const char* cmd = CLIHistory_getIndexCommand(&in->history);

    CLIHistory_incIndex(&in->history);

    if (cmd != NULL) {
        CLIInput_clearCurrCommand(in);
        CLIInput_printNewCommand(in, cmd);
    }
}

static void CLIInput_scrollHistoryDown(CLIInput_t* in)
{
    CLIHistory_decIndex(&in->history);

    const char* cmd = CLIHistory_getIndexCommand(&in->history);

    CLIInput_clearCurrCommand(in);

    if (cmd != NULL) {
        CLIInput_printNewCommand(in, cmd);
    }
}
#endif

#if CLI_CMD_EDIT_ENABLE
static void CLIInput_handleBackspace(CLIInput_t* in)
{
    if (in->bufferInsPos == 0) {
        return;
    }

    --in->bufferInsPos;
    --in->bufferEndPos;

    if (in->echoEnable) {
        CLIConsole_putchar('\b');   // move cursor left
    }

    CLIConsole_print("\x1b[1P"); // DCH: delete 1 symbol under the cursor, shift the rest to the left

//    CLIConsole_putchar(CLI_ASCII_BACKSPACE);    //// delete 1 character and
//
//    if (in->echoEnable) {
//        CLIConsole_print("\033[0P");                //// shift the rest to the left
//    }

    if (in->bufferInsPos != in->bufferEndPos) {
        uint16_t count = in->bufferEndPos - in->bufferInsPos;

        for (uint16_t idx = 0; idx < count; ++idx) {
            in->buffer[in->bufferInsPos + idx] = in->buffer[in->bufferInsPos + idx + 1];
        }
    }
}

static void CLIInput_handleDelete(CLIInput_t* in)
{
    if (in->bufferInsPos == in->bufferEndPos) {
        return;
    }

    if (in->echoEnable) {
        CLIConsole_print("\x1b[1P");    /// delete 1 character and shift the rest to the left
    }

    --in->bufferEndPos;

    uint16_t count = in->bufferEndPos - in->bufferInsPos;

    for (uint16_t idx = 0; idx < count; ++idx) {
        in->buffer[in->bufferInsPos + idx] = in->buffer[in->bufferInsPos + idx + 1];
    }
}

static void CLIInput_moveLeft(CLIInput_t* in)
{
    if (in->bufferInsPos == 0) {
        return;
    }

    if (in->echoEnable) {
        CLIConsole_putchar('\b');
    }

    --in->bufferInsPos;
}

static void CLIInput_moveRight(CLIInput_t* in)
{
    if (in->bufferInsPos >= in->bufferEndPos) {
        return;
    }

    if (in->echoEnable) {
        CLIConsole_print("\033[1C");    /// Cursor forward (Right) by <1>
    }

    ++in->bufferInsPos;
}


static void CLIInput_handleSymbolReplaceMode(CLIInput_t* in, char symbol)
{
    if (in->bufferInsPos == in->bufferEndPos) {
        in->buffer[in->bufferEndPos] = symbol;
        ++in->bufferEndPos;
    } else {
        in->buffer[in->bufferInsPos] = symbol;
    }

    ++in->bufferInsPos;
}

static void CLIInput_insModeChange(CLIInput_t* in)
{
    in->replaceMode = !in->replaceMode;

    if (in->replaceMode) {
        cursorStyle(ATTR_CURSOR_BLINKING_BLOCK);
        in->handleSymbol = CLIInput_handleSymbolReplaceMode;
    } else {
        cursorStyle(ATTR_CURSOR_BLINKING_BAR);
        in->handleSymbol = CLIInput_handleSymbolInsMode;
    }
}

static void CLIInput_moveHome(CLIInput_t* in)
{
    while (in->bufferInsPos != 0) {
        CLIConsole_putchar(CLI_ASCII_BACKSPACE);

        --in->bufferInsPos;
    }
}

static void CLIInput_moveEnd(CLIInput_t* in)
{
    while (in->bufferInsPos < in->bufferEndPos) {
        CLIConsole_print("\033[1C");    /// Cursor forward (Right) by <1>

        ++in->bufferInsPos;
    }
}
#endif

static void CLIInput_handleCtrl(CLIInput_t* in, uint16_t key)
{
    switch (key) {
#if (CLI_HISTORY_DEEP != 0)
    case cliKeyUP:    CLIInput_scrollHistoryUp(in);   break;
    case cliKeyDN:    CLIInput_scrollHistoryDown(in); break;
#endif

#if CLI_CMD_EDIT_ENABLE
    case cliKeyLEFT:  CLIInput_moveLeft(in);          break;
    case cliKeyRIGHT: CLIInput_moveRight(in);         break;
    case cliKeyINS:   CLIInput_insModeChange(in);     break;
    case cliKeyHOME:  CLIInput_moveHome(in);          break;
    case cliKeyEND:   CLIInput_moveEnd(in);           break;
#endif

    default:         return;
    }
}

static void CLIInput_handleEsc(CLIInput_t* in)
{
    (void) in;
}

static void CLIInput_handleTab(CLIInput_t* in)
{
    (void) in;
}

static void CLIInput_echo(CLIInput_t* in, int ch)
{
    if (in->echoEnable == false) {
        return;
    }

    CLIConsole_putchar(ch);
}

static void CLIInput_handleEnter(CLIInput_t* in)
{
    CLIInput_echo(in, CLI_ASCII_ENTER);    /// echo

    in->buffer[in->bufferEndPos] = '\0';

#if (CLI_HISTORY_DEEP != 0)
    CLIHistory_push(&in->history, in->buffer);
#endif

    CLIInput_parsing(in);
}

static void CLIInput_handleOther(CLIInput_t* in, char c)
{
    in->handleSymbol(in, c);

    CLIInput_echo(in, c);    /// echo

    if (in->bufferEndPos >= CLI_MANAGER_BUFFER_LEN) {
        CLIInput_reset(in);
    }
}

static bool CLIInput_handleCharacter(CLIInput_t* in, char c)
{
    switch (c) {
#if CLI_CMD_EDIT_ENABLE
    case CLI_ASCII_BACKSPACE: CLIInput_handleBackspace(in); return false;
    case CLI_ASCII_DEL:       CLIInput_handleDelete(in);    return false;
#endif
    case CLI_ASCII_ESC:       CLIInput_handleEsc(in);       return false;
    case CLI_ASCII_TAB:       CLIInput_handleTab(in);       return false;
    case CLI_ASCII_ENTER:     CLIInput_handleEnter(in);     return true;
    default:                  CLIInput_handleOther(in, c);  return false;
    }
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initializes CLIInput_t object
 * 
 * @param in Pointer to CLIInput_t object
 */
void CLIInput_init(CLIInput_t* in)
{
    memset(in, 0, sizeof(CLIInput_t));

    in->handleSymbol = CLIInput_handleSymbolInsMode;

#if (CLI_HISTORY_DEEP != 0)
    CLIHistory_init(&in->history);
#endif
}

/**
 * @brief Deitializes CLIInput_t object
 * 
 * @param in Pointer to CLIInput_t object
 */
void CLIInput_deInit(CLIInput_t* in)
{
#if (CLI_HISTORY_DEEP != 0)
    CLIHistory_deInit(&in->history);
#endif
}

/**
 * @brief Sets the echo enable flag
 *
 * @param in    Pointer to CLIInput_t object
 * @param value True for enabling echo and false for disabling
 */
void CLIInput_echoEnable(CLIInput_t* in, bool value)
{
    in->echoEnable = value;
}

/**
 * @brief Returns current value of echoEnable flag
 *
 * @param in Pointer to CLIInput_t object
 * @return   Current value of echoEnable flag
 */
bool CLIInput_echoEnabled(CLIInput_t* in)
{
    return in->echoEnable;
}

/**
 * @brief Refreshes the keyboard and handles input key code
 * 
 * @param in Pointer to CLIInput_t object
 * @return   True if the last key was Enter, otherwise false
 */
bool CLIInput_refresh(CLIInput_t* in)
{
    while (CLIKeyboard_kbhit() == true) {
        uint16_t key = CLIKeyboard_getKey();

        if (key > 0xFF) {
            CLIInput_handleCtrl(in, key);
            return false;
        } else {
            if (CLIInput_handleCharacter(in, (char)key) == true) {
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Use this function if you need to emulate input some command
 * 
 * @param in  Pointer to CLIInput_t object
 * @param cmd Pointer to the string with command
 */
void CLIInput_pushCommand(CLIInput_t* in, const char* cmd)
{
    CLIInput_reset(in);

    while (true) {
        if ((*cmd == '\0') ||
            (*cmd == '\r') ||
            (*cmd == '\n')) {
            break;
        }

        CLIInput_handleCharacter(in, *cmd++);
    }

    CLIInput_handleCharacter(in, '\n');
}

/**
 * @brief Resets the input object to default state
 * 
 * @param in Pointer to CLIInput_t object
 */
void CLIInput_reset(CLIInput_t* in)
{
    CLIKeyboard_reset();

    in->bufferInsPos = 0;
    in->bufferEndPos = 0;

    in->argC = 0;
    memset(&in->argV[0], 0, sizeof(in->argV));
}

/**
 * @brief Returns the number of arguments to the command that was last parsed
 * 
 * @param in Pointer to CLIInput_t object
 * @return   Number of arguments including command name
 */
int CLIInput_getArgC(CLIInput_t* in)
{
    return in->argC;
}

/**
 * @brief Returns pointers to arguments to the command
 * 
 * @param in            Pointer to CLIInput_t object
 * @return const char** Pointer to the list of arguments including command name
 */
const char** CLIInput_getArgV(CLIInput_t* in)
{
    if (in->argC == 0) {
        in->argV[0] = "";
    }

    return (const char**)&in->argV[0];
}
