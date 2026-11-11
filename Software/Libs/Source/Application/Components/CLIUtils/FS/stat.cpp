#include "Components/CLIUtils/FS/stat.hpp"
#include "Components/CLIUtils/FS/fsUtilities.h"
#include "CLI/CLIConsole.h"
#include "FileSystem.hpp"

#include "ff.h"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <inttypes.h>

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Private code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

void stat_run(int argc, const char** argv)
{
    (void) argc;
    (void) argv;

    FATFS* fs = FS::FileSystem::GetInstance().getFatFS(0);

    if (fs == NULL) {
        return;
    }

    uint32_t freeClust;
    FRESULT res = f_getfree("", &freeClust, &fs);
    if(res != FR_OK) {
        CLIConsole_print("f_getfree() failed, res = %d\r\n", res);
        return;
    }

    uint32_t totalBlocks = (fs->n_fatent - 2) * fs->csize;
    uint32_t freeBlocks  = freeClust * fs->csize;

    totalBlocks *= FF_MIN_SS;
    freeBlocks  *= FF_MIN_SS;

    char sizeString[16] = { 0 };

    fsUtilities_UI32toString(totalBlocks, sizeString);
    CLIConsole_print("total : %s\n", sizeString);

    fsUtilities_UI32toString(freeBlocks, sizeString);
    CLIConsole_print("free  : %s\n", sizeString);

    fsUtilities_UI32toString(totalBlocks - freeBlocks, sizeString);
    CLIConsole_print("used  : %s\n", sizeString);
}
