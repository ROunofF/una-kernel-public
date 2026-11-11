/**
 ******************************************************************************
 * @file    CLIKeyboard.c
 * @brief   This is a dispatcher of key codes that received from terminal.
 *          It handles simple keys and ESC-sentences for functional and control
 *          keys.
 * 
 ******************************************************************************
 *
 ******************************************************************************
 */

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

typedef struct {
    int      keyCode;
    CLIKey_t key;
} CLIKeyMap_t;

////////////////////////////////////////////////////////////////////////////////
/// Private data
////////////////////////////////////////////////////////////////////////////////

static CLIKeyboard_t* mKB;

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

static bool CLIKeyboard_parseCode(int keyCode, CLIKey_t* key)
{
    static CLIKeyMap_t map[] = {
        {0x11, cliKeyF1},
        {0x12, cliKeyF2},
        {0x13, cliKeyF3},
        {0x14, cliKeyF4},
        {0x15, cliKeyF5},
        {0x17, cliKeyF6},
        {0x18, cliKeyF7},
        {0x19, cliKeyF8},
        {0x20, cliKeyF9},
        {0x21, cliKeyF10},
        {0x23, cliKeyF11},
        {0x24, cliKeyF12},
        {0x02, cliKeyINS},
        {0x05, cliKeyPGUP},
        {0x06, cliKeyPGDN},
        {0x01, cliKeyHOME},
        {0x04, cliKeyEND},
        {0x0D, cliKeyLEFT},
        {0x0C, cliKeyRIGHT},
        {0x0A, cliKeyUP},
        {0x0B, cliKeyDN},
    };

    for (uint8_t idx = 0; idx < ARRAY_SIZE(map); ++idx) {
        if (map[idx].keyCode == keyCode) {
            *key = map[idx].key;
            return true;
        }
    }

    return false;
}

static int8_t CLIKeyboard_receiveSequence(uint8_t* buff, uint8_t size)
{
    int8_t count = 0;
    
    TimerSysMs_t t;
    TimerSysMs_startWithValue(&t, 50);

    while (true) {
        if (TimerSysMs_check(&t)) {
            return (count == 0) ? 0 : -1;
        }

        if (mKB->config.kbHit() == false) {
            continue;
        }

        char c;
        mKB->config.getChar((uint8_t*)&c, 1);

        if (c == '~') {
            return count;
        }

        buff[count++] = c;

        if ((c >= 'A') && (c <= 'D')) {
            return count;
        }

        if (count >= size) {
            return -1;
        }
    }
}

static void CLIKeyboard_receiving()
{
    uint16_t cliKey;

    char c;
    mKB->config.getChar((uint8_t*)&c, 1);

    // uncomment the source code below to see the keycodes
    // CLIConsole_print("0x%02X\n", key);
    // return;

    if (c == 0x1B) {
        //////////////////////////////
        /// Some special ASCII symbol
        /// Like ESC, F1-F12 and so on
        //////////////////////////////

        uint8_t buffer[5];
        int8_t  count = CLIKeyboard_receiveSequence(buffer, sizeof(buffer));

        if (count < 0) {
            return;
        }

        if (count == 0) {
            cliKey = 0x1B;
        } else {
            int keyCode;
            if (sscanf((const char*) buffer, "[%X", &keyCode) != 1) {
                return;
            }

            CLIKey_t key;
            if (CLIKeyboard_parseCode(keyCode, &key) == false) {
                return;
            }

            cliKey = (uint16_t) key;
        }
    } else {
        //////////////////////////////
        /// Usual ASCII symbol
        //////////////////////////////

        cliKey = c;
    }

    FIFO_pushBuffer(&mKB->fifo, (uint8_t*) &cliKey, sizeof(cliKey));
}

static void CLIKeyboard_refresh()
{
    while (mKB->config.kbHit() != 0) {
        CLIKeyboard_receiving();
    }
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Initializes the keyboard component
 * 
 * @param cfg Pointer to CLIKeyboardConfig_t object with pointers to some functions
 */
void CLIKeyboard_init(CLIKeyboard_t* kb, CLIKeyboardConfig_t* cfg)
{
    kb->config = *cfg;

    FIFO_init(&kb->fifo, kb->fifoBuffer, sizeof(kb->fifoBuffer));

    CLIKeyboard_set(kb);

    CLIKeyboard_reset();
}

void CLIKeyboard_set(CLIKeyboard_t* kb)
{
    mKB = kb;
}

/**
 * @brief Indicated that some keys were pressed
 * 
 * @return True if keys were pressed otherwise false
 */
bool CLIKeyboard_kbhit()
{
    CLIKeyboard_refresh();

    return (FIFO_getAvailForRead(&mKB->fifo) != 0);
}

/**
 * @brief Returns key code for input queue
 * 
 * @return uint16_t Key code from input queue or zero if there is no key 
 */
uint16_t CLIKeyboard_getKey()
{
    if (FIFO_getAvailForRead(&mKB->fifo) == 0) {
        return 0;
    }

    uint16_t key;

    FIFO_popBuffer(&mKB->fifo, (uint8_t*) &key, sizeof(key));

    return key; 
}

/**
 * @brief Reset keyboard input
 * 
 */
void CLIKeyboard_reset()
{
    while (mKB->config.kbHit()) {
        uint8_t c;
        mKB->config.getChar(&c, 1);
    }

    FIFO_reset(&mKB->fifo);
}
