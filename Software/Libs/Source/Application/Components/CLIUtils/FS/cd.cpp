#define LOG_MODULE_PRX      "fs.cd"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "Components/CLIUtils/FS/cd.hpp"
#include "CLI/CLIConsole.h"
#include "FileSystem/Folder.h"

#include "ff.h"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

bool cd_run(int argc, const char** argv)
{
    if (argc != 2) {
        return false;
    }

    char currDir[FF_MAX_LFN];

    const char* dir = argv[1];

    if (dir[0] == '/') {
        ///////////////////////////////////////
        /// Set absolute path
        ///////////////////////////////////////

        FRESULT res = f_chdir(dir);
        if (res != FR_OK) {
            LOG_ERROR("fs error (%d)\n", (int)res);
            return false;
        }

        return true;
    } else if (strcmp(dir, "..") == 0) {
        ///////////////////////////////////////
        /// Go back to the previous level
        ///////////////////////////////////////

        FRESULT res = f_getcwd(currDir, sizeof(currDir));
        if (res != FR_OK) {
            LOG_ERROR("fs error (%d)\n", (int)res);
            return false;
        }

        int32_t idx = strlen(currDir) - 1;
        while (idx >= 0) {
            if (currDir[idx] == '/') {
                currDir[idx] = '\0';

                FRESULT res = f_chdir((strlen(currDir) == 0) ? "/" : currDir);
                if (res != FR_OK) {
                    LOG_ERROR("fs error (%d)\n", (int)res);
                    return false;
                }

                return true;
            }

            idx--;
        }

        return false;
    }

    ///////////////////////////////////////
    /// Go to the local directory
    ///////////////////////////////////////

    FRESULT res = f_getcwd(currDir, sizeof(currDir));
    if (res != FR_OK) {
        LOG_ERROR("fs error (%d)\n", (int)res);
        return false;
    }

    if (currDir[strlen(currDir) - 1] != '/') {
        strcat(currDir, "/");
    }

    strcat(currDir, dir);

    LOG_INFO("cd path=%s\n", currDir);

    res = f_chdir(currDir);
    if (res != FR_OK) {
        LOG_ERROR("fs error (%d)\n", (int)res);
        return false;
    }

    return true;
}
