#ifndef __CLIKeyboard_h__
#define __CLIKeyboard_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "Porting.h"
#include "CLIOptions.h"
#include "CLIDataTypes.h"
#include "FIFO/FIFO.h"

/////////////////////////////////////////////////////////////////////////////
// Defines
/////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////
// Data types
/////////////////////////////////////////////////////////////////////////////

typedef enum {
    cliKeyF1    = 0x0100,
    cliKeyF2    = 0x0200,
    cliKeyF3    = 0x0300,
    cliKeyF4    = 0x0400,
    cliKeyF5    = 0x0500,
    cliKeyF6    = 0x0600,
    cliKeyF7    = 0x0700,
    cliKeyF8    = 0x0800,
    cliKeyF9    = 0x0900,
    cliKeyF10   = 0x0A00,
    cliKeyF11   = 0x0B00,
    cliKeyF12   = 0x0C00,
    cliKeyINS   = 0x0D00,
    cliKeyPGUP  = 0x0E00,
    cliKeyPGDN  = 0x0F00,
    cliKeyHOME  = 0x1000,
    cliKeyEND   = 0x1100,
    cliKeyLEFT  = 0x1200,
    cliKeyRIGHT = 0x1300,
    cliKeyUP    = 0x1400,
    cliKeyDN    = 0x1500,
} CLIKey_t;

typedef struct {
    CLIConsole_getInQueue_t kbHit;
    CLIConsole_recv_t       getChar;
} CLIKeyboardConfig_t;

typedef struct {
    CLIKeyboardConfig_t config;
    FIFO_t              fifo;
    uint8_t             fifoBuffer[CLI_KEYBOARD_BUFFER];
} CLIKeyboard_t;

/////////////////////////////////////////////////////////////////////////////
// Public code
/////////////////////////////////////////////////////////////////////////////

void     CLIKeyboard_init(CLIKeyboard_t* kb, CLIKeyboardConfig_t* cfg);
void     CLIKeyboard_set(CLIKeyboard_t* kb);
bool     CLIKeyboard_kbhit();
uint16_t CLIKeyboard_getKey();
void     CLIKeyboard_reset();

#ifdef __cplusplus
 }
#endif

#endif
