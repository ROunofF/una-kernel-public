#include "Components/CLIUtils/utilClearScreen.h"
#include "CLI/CLIConsole.h"
#include "CLI/CLIInput.h"
#include "CLI/CLIParser.h"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

////////////////////////////////////////////////////////////////////////////////
/// Defines
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private data
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

static const char* utilClearScreen_getName(ICLICommand_t* cmd)
{
    return "cls";
}

static const char* utilClearScreen_getDesc(ICLICommand_t* cmd)
{
    return "clear screen";
}

static const char* utilClearScreen_getHelp(ICLICommand_t* cmd)
{
    return "sends an ASCII sequence to clear the screen";
}

static void utilClearScreen_execute(ICLICommand_t* cmd, CLIInput_t* input, int argc, const char** argv)
{
    (void) cmd;
    (void) input;
    (void) argc;
    (void) argv;

    CLIConsole_print("\033[2J\033[;H");
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void utilClearScreen_init(UtilClearScreen_t* util)
{
    memset(util, 0, sizeof(UtilClearScreen_t));

    util->intf.execute = utilClearScreen_execute;
    util->intf.getName = utilClearScreen_getName;
    util->intf.getDesc = utilClearScreen_getDesc;
    util->intf.getHelp = utilClearScreen_getHelp;
    util->intf.ctx     = util;
}

ICLICommand_t* utilClearScreen_getIntf(UtilClearScreen_t* util)
{
    return &util->intf;
}
