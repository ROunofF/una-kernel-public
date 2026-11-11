#ifndef __CLIConsole_h__
#define __CLIConsole_h__

#include "Porting.h"
#include "CLIOptions.h"
#include "CLIDataTypes.h"
#include "CLIKeyboard.h"

#ifdef __cplusplus
 extern "C" {
#endif

#define F_BLACK                 (30)
#define F_RED                   (31)
#define F_GREEN                 (32)
#define F_YELLOW                (33)
#define F_BLUE                  (34)
#define F_MAGENTA               (35)
#define F_CYAN                  (36)
#define F_WHITE                 (37)

#define B_BLACK                 (40)
#define B_RED                   (41)
#define B_GREEN                 (42)
#define B_YELLOW                (43)
#define B_BLUE                  (44)
#define B_MAGENTA               (45)
#define B_CYAN                  (46)
#define B_WHITE                 (47)

//Format text
#define ATTR_RESET              (0)
#define ATTR_BRIGHT             (1)
#define ATTR_DIM                (2)
#define ATTR_ITALIC             (3)
#define ATTR_UNDERLINE          (4)
#define ATTR_BLINK              (5)
#define ATTR_INVERSE            (7)
#define ATTR_HIDDEN             (8)
#define ATTR_STRIKETHROUGH      (9)

#define ATTR_CURSOR_USER_SHAPE          (0)
#define ATTR_CURSOR_BLINKING_BLOCK      (1)
#define ATTR_CURSOR_STEADY_BLOCK        (2)
#define ATTR_CURSOR_BLINKING_UNDERLINE  (3)
#define ATTR_CURSOR_STEADY_UNDERLINE    (4)
#define ATTR_CURSOR_BLINKING_BAR        (5)
#define ATTR_CURSOR_STEADY_BAR          (6)

#define CLI_ASCII_BACKSPACE     (8)
#define CLI_ASCII_TAB           (9)
#define CLI_ASCII_ESC           (27)
#define CLI_ASCII_DEL           (127)
#define CLI_ASCII_ENTER         (10)
#define CLI_ASCII_CTRL_Z        (0x1A)

#define home()                  CLIConsole_print("\033[;H") //Move cursor to the indicated row, column (origin at 1,1)
#define clrscr()                CLIConsole_print("\033[2J\033[;H") //lear the screen, move to (1,1)
#define gotoxy(x,y)             CLIConsole_print("\033[%d;%dH", (y), (x));
#define cursorHide()            CLIConsole_print("\033[?25l");
#define cursorShow()            CLIConsole_print("\033[?25h");
#define colorReset()            CLIConsole_print("\033[0m")
#define setDisplayAttr(attr)    CLIConsole_print("\033[%dm", (attr))
#define resetDisplayAttr(attr)  CLIConsole_print("\033[%dm", (attr) + 20)
#define cursorStyle(style)      CLIConsole_print("\033[%d q", (style));

typedef struct {
    CLIConsole_connected_t  connected;
    CLIConsole_send_t       send;
    CLIConsole_getInQueue_t getInQueue;
    CLIConsole_recv_t       recv;
} CLIConsoleConfig_t;

typedef struct {
    CLIConsoleConfig_t config;
    CLIKeyboard_t      kb;
} CLIConsole_t;

void     CLIConsole_init(CLIConsole_t* console, CLIConsoleConfig_t* cfg);
void     CLIConsole_set(CLIConsole_t* console);
void     CLIConsole_clearRxQueue();
bool     CLIConsole_kbhit(void);
int      CLIConsole_putchar(int ch);
int      CLIConsole_getchar(void);
void     CLIConsole_print(const char* format, ...);
void     CLIConsole_send(const uint8_t* buff, uint16_t len);
bool     CLIConsole_connected();
uint16_t CLIConsole_avail();
uint16_t CLIConsole_recv(uint8_t* buff, uint16_t len);

#ifdef __cplusplus
 }
#endif

#endif
