#include "Components/CLIUtils/FS/md5.hpp"
#include "CLI/CLIConsole.h"

#include "ff.h"
#include "MD5/md5.hpp"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Private code
////////////////////////////////////////////////////////////////////////////////

static void md5_computeA(FIL* fd)
{
    uint8_t buff[512];

    MD5 md5;
    md5.init();

    while (f_eof(fd) == false) {
        UINT rdCount;
        if (f_read(fd, buff, sizeof(buff), &rdCount) != FR_OK) {
            CLIConsole_print("failed to read the file\n");
            return;
        }

        md5.update(buff, (size_t) rdCount);
    }

    MD5::Digest digest = md5.finalize();

    char str[33] = { 0 };
    for (uint8_t idx = 0; idx < ARRAY_SIZE(digest.bytes); ++idx) {
        char tmp[3];

        sprintf(tmp, "%02X", digest.bytes[idx]);
        strcat(str, tmp);
    }

    CLIConsole_print("%s\n", str);
}

static void md5_compute(const char* fileName)
{
    FIL* fd = (FIL*) malloc(sizeof(FIL));
    if (fd == NULL) {
        CLIConsole_print("failed to allocate FIL object\n");
        return;
    }

    if (f_open(fd, fileName, FA_READ) != FR_OK) {
        CLIConsole_print("failed to open the file\n");
        free(fd);
        return;
    }

    md5_computeA(fd);

    f_close(fd);
    free(fd);
}

////////////////////////////////////////////////////////////////////////////////
//// Public code
////////////////////////////////////////////////////////////////////////////////

void md5_run(int argc, const char** argv)
{
    char filename[FF_MAX_LFN * 2] = { 0 };

    if (argv[1][0] != '/') {
        char currDir[FF_MAX_LFN] = { 0 };

        if (f_getcwd(currDir, sizeof(currDir)) != FR_OK) {
            CLIConsole_print("could not get current work directory\n");
            return;
        }

        sprintf(filename, "%s/%s", currDir, argv[1]);
    } else {
        sprintf(filename, "%s", argv[1]);
    }

    md5_compute(filename);
}
