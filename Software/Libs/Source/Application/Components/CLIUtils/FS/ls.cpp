#include "Components/CLIUtils/FS/ls.hpp"
#include "CLI/CLIConsole.h"
#include "FileSystem/Folder.h"
#include "Components/CLIUtils/FS/fsUtilities.h"

#include "ff.h"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

#define LS_COLUMN_SEPARATOR   "   "

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

static void ls_runA(int argc, const char** argv, bool showFolder)
{
    char currDir[FF_MAX_LFN] = { 0 };

    if (f_getcwd(currDir, sizeof(currDir)) != FR_OK) {
        CLIConsole_print("could not get current work directory\n");
        return;
    }

    char dir[FF_MAX_LFN * 2] = { 0 };
    if (argc == 2) {
        if (argv[1][0] != '/') {
            sprintf(dir, "%s/%s", currDir, argv[1]);
        } else {
            sprintf(dir, "%s", argv[1]);
        }
    } else {
        sprintf(dir, "%s", currDir);
    }

    FolderRdCtx_t ctx;
    if (Folder_readingStart(&ctx, dir) == false) {
        CLIConsole_print("failed to open specified directory\n");
        return;
    }

    while (true) {
        FolderItem_t item;
        if (Folder_readingNext(&ctx, &item) == false) {
            break;
        }

        if (showFolder == true) {
            if (item.isDir != true) {
                continue;
            }
        } else {
            if (item.isDir == true) {
                continue;
            }          
        }
        
        // if (item.isHiden == true)  {
        //     continue;
        // }

        // if (item.isSystem == true)  {
        //     continue;
        // }

        CLIConsole_print("%04d/%02d/%02d %02d:%02d:%02d", item.ts.tm_year + 2000,
                                                          item.ts.tm_mon,
                                                          item.ts.tm_mday,
                                                          item.ts.tm_hour,
                                                          item.ts.tm_min,
                                                          item.ts.tm_sec);

        if (showFolder == true) {
            CLIConsole_print(LS_COLUMN_SEPARATOR);
            CLIConsole_print("<DIR>");
            CLIConsole_print(LS_COLUMN_SEPARATOR);
            CLIConsole_print("               ");
        } else {
            CLIConsole_print(LS_COLUMN_SEPARATOR);
            CLIConsole_print("     ");
            CLIConsole_print(LS_COLUMN_SEPARATOR);

            char sizeString[16] = { 0 };
            fsUtilities_UI32toString(item.size, sizeString);

            CLIConsole_print("%s", sizeString);
        }
        
        CLIConsole_print(LS_COLUMN_SEPARATOR);
        CLIConsole_print("%s\n", item.name);
    }
}


////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void ls_run(int argc, const char** argv)
{
    ls_runA(argc, argv, true);
    ls_runA(argc, argv, false);
}
