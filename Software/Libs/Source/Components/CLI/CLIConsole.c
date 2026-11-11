/**
 ******************************************************************************
 * @file    CLIConsole.c
 * @brief   This component is a set of basic I/O functions
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "CLI/CLIConsole.h"
#include "CLI/CLIKeyboard.h"
#include "TimerSysMs.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

////////////////////////////////////////////////////////////////////////////////
/// Define
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private data
////////////////////////////////////////////////////////////////////////////////

static CLIConsole_t* mConsole;

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initializes a set of basic I/O functions and a keyboard component.
 * 
 * @param cliConfig 
 */
void CLIConsole_init(CLIConsole_t* console, CLIConsoleConfig_t* cliConfig)
{
    console->config = *cliConfig;

    CLIKeyboardConfig_t kbConfig = { 0 };
    kbConfig.kbHit   = console->config.getInQueue;
    kbConfig.getChar = console->config.recv;
    
    CLIKeyboard_init(&console->kb, &kbConfig);
}

void CLIConsole_set(CLIConsole_t* console)
{
    mConsole = console;
    CLIKeyboard_set(&mConsole->kb);
}

void CLIConsole_clearRxQueue(void)
{
    while (true) {
        if (!CLIConsole_kbhit()) {
            continue;
        }

        CLIConsole_getchar();
    }
}

/**
 * @brief Indicates that there is data in the input queue
 * 
 * @return True if data is available, false otherwise.
 */
bool CLIConsole_kbhit(void)
{
    return (mConsole->config.getInQueue() != 0);
}

/**
 * @brief Sends one character to the terminal
 * 
 * @param ch Character value
 * @return   Character value
 */
int CLIConsole_putchar(int ch)
{
    uint8_t value = ch;

    mConsole->config.send(&value, 1);

    return ch;
}

/**
 * @brief Returns character value from the input queue
 * 
 * @return Character value
 */
int CLIConsole_getchar(void)
{
    uint8_t ch = 0;

    TimerSysMs_t timeout;
    TimerSysMs_startWithValue(&timeout, TIMER_SYS_MS_SECONDS(30));

    while (TimerSysMs_check(&timeout) == false) {
        if (mConsole->config.getInQueue() != 0) {
            mConsole->config.recv(&ch, 1);
            break;
        }
        else {
            WAIT(2);
        }
    }

    return ch;
}

/**
 * @brief Prints 
 * 
 * @param format pointer to a null-terminated string (C-string) that is written
 *               to terminal. It consists of characters along with optional
 *               format specifiers starting with %
 * @param ...    Other additional arguments specifying the data to be printed.
 *               They occur in a sequence according to the format specifier.
 */
void CLIConsole_print(const char* format, ...)
{
    if (!CLIConsole_connected()) {
        return;
    }

    va_list ap;
    va_start(ap, format);

    /// Compute text length
    /// Also, take into account the null-terminal character
    int count = vsnprintf(NULL, 0, format, ap) + 1;

    /// Allocate buffer memory and print to it
    if (count > 0) {
        static const int tmpSize = 256;
        if (count < tmpSize) {
            char text[tmpSize];
            vsnprintf(text, count, format, ap);
            mConsole->config.send((const uint8_t*) text, count - 1);
        } else {
            char* text = (char*) malloc(count);
            if (text != NULL) {
                vsnprintf(text, count, format, ap);

                mConsole->config.send((const uint8_t*) text, count - 1);

                free(text);
            }
        }
    }

    va_end(ap);
}

/**
 * @brief Sends data (text or binary) to terminal
 * 
 * @param buff Pointer to buffer
 * @param len  Buffer length
 */
void CLIConsole_send(const uint8_t* buff, uint16_t len)
{
    if (!CLIConsole_connected()) {
        return;
    }

    mConsole->config.send(buff, len);
}

bool CLIConsole_connected()
{
    return mConsole->config.connected();
}

/**
 * @brief Returns number of bytes in the input queue
 * 
 * @return Number of bytes in the input queue
 */
uint16_t CLIConsole_avail(void)
{
    if (!CLIConsole_connected()) {
        return 0;
    }

    return mConsole->config.getInQueue();
}

/**
 * @brief Receives data from the input queue
 * 
 * @param buff Pointer to buffer
 * @param len  Buffer length
 * @return     The number of bytes actually written to the specified buffer
 */
uint16_t CLIConsole_recv(uint8_t* buff, uint16_t len)
{
    if (!CLIConsole_connected()) {
        return 0;
    }

    return mConsole->config.recv(buff, len);
}
