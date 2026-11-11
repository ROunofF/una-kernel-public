#define LOG_MODULE_PRX      "fs.cat"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Components/CLIUtils/FS/cat.hpp"
#include "CLI/CLIConsole.h"
#include "CLI/CLIParser.h"
#include "ff.h"
#include "TimerSysMs.h"
#include "SwTimer.hpp"
#include "Sink/PushFileWriter.hpp"
#include "OS.hpp"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include <memory>
#include <string>

////////////////////////////////////////////////////////////////////////////////
//// Data types
////////////////////////////////////////////////////////////////////////////////

typedef struct {
    char     fileName[FF_MAX_LFN];
    bool     showLineNumber;
    bool     modeBinary;
    uint8_t  modeBinaryWidth;
    bool     modeBinaryASCII;
    uint16_t pageLines;
    uint32_t offset;
} cat_t;

////////////////////////////////////////////////////////////////////////////////
//// Private data
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//// Private code
////////////////////////////////////////////////////////////////////////////////

static void cat_default(cat_t* settings)
{
    settings->fileName[0]     = '\0';
    settings->modeBinary      = false;
    settings->showLineNumber  = false;
    settings->modeBinaryWidth = 16;
    settings->modeBinaryASCII = true;
    settings->pageLines       = 0;
    settings->offset          = 0;
}

static void cat_modeTxt(FIL* fd, cat_t* settings)
{
    uint16_t pageLines  = 0;
    uint16_t lineNumber = 1;
    bool     showLine   = settings->showLineNumber;

    while (f_eof(fd) == false) {
        uint8_t buff[128];

        UINT rdCount;
        if (f_read(fd, buff, sizeof(buff), &rdCount) != FR_OK) {
            break;
        }

        OS::Delay(2);

        for (uint8_t idx = 0; idx < rdCount; ++idx) {
            if (showLine == true) {
                showLine = false;
                CLIConsole_print("%6d:  ", lineNumber);
                lineNumber++;
            }

            char symbol = buff[idx];

            CLIConsole_print("%c", symbol);

            if (symbol == '\n') {
                showLine = settings->showLineNumber;
                ++pageLines;
            }

            if ((settings->pageLines != 0) && (pageLines >= settings->pageLines)) {
                pageLines = 0;

                CLIConsole_print("press any key to continue...\r");

                while (CLIConsole_kbhit() == false) {
                    OS::Delay(100);
                }

                CLIConsole_print("                             ");
                CLIConsole_print("\r");

                char s = CLIConsole_getchar();

                if (s == CLI_ASCII_CTRL_Z) {
                    return;
                }
            }
        }
    }
}

static void cat_printString(uint8_t* buff,
                            uint8_t  len,
                            uint8_t  width,
                            bool     showASCII,
                            char*    str)
{
        
    for (uint8_t idx = 0; idx < len; ++idx) {
        sprintf(str, "%.2X ", buff[idx]);
        str += 3;
    }

    if (showASCII) {
        for (uint8_t i = len; i < width; i++) {
            sprintf(str, "   ");
            str += 3;
        }

        for (uint8_t i = 0; i < 5; i++) {
            sprintf(str, " ");
            str += 1;
        }

        for (uint8_t i = 0; i < len; i++) {
            char s;
            if ((isprint(buff[i])) && 
                (buff[i] != 0x09)  &&
                (buff[i] != 0x0A)  &&
                (buff[i] != 0x0D))
            {
                s = (char) buff[i];
            }
            else {
                s = '.';
            }

            sprintf(str, "%c", s);
            str += 1;
        }
    }
}

static void cat_modeBin(FIL* fd, cat_t* settings)
{
    char printString[128];

    uint16_t pageLines = 0;
    uint16_t lineNumber  = 1;
    bool     showLine    = settings->showLineNumber;

    uint8_t  buff[512];
    uint8_t  maxCount = settings->modeBinaryWidth * 10;

    ASSERT(maxCount <= sizeof(buff));

    while (f_eof(fd) == false) {
        UINT rdCount;
        if (f_read(fd, buff, maxCount, &rdCount) != FR_OK) {
            break;
        }

        OS::Delay(2);

        uint8_t i = 0;
        while (i < rdCount) {
            uint8_t len = rdCount - i;
            if (len > settings->modeBinaryWidth) {
                len = settings->modeBinaryWidth;
            }
            
            cat_printString(&buff[i],
                            len,
                            settings->modeBinaryWidth,
                            settings->modeBinaryASCII,
                            printString);

            if (showLine == true) {
                CLIConsole_print("%6d:  ", lineNumber);
                lineNumber++;
            }

            CLIConsole_print("%s\n", printString);

            if ((settings->pageLines != 0) && (++pageLines >= settings->pageLines)) {
                pageLines = 0;

                CLIConsole_print("press any key to continue...\r");

                while (CLIConsole_kbhit() == false) {
                    OS::Delay(100);
                }

                CLIConsole_print("                             ");
                CLIConsole_print("\r");

                char s = CLIConsole_getchar();

                if (s == CLI_ASCII_CTRL_Z) {
                    return;
                }
            }

            i += len;
        }
    }
}

static void cat_create(Sink::PushFileWriter& stream, bool echoEnabled, uint8_t* buff, uint16_t buffSize)
{
    Driver::SwTimer t(TIMER_SYS_MS_SECONDS(60));
    while (true) {
        if (t.expired()) {
            return;
        }

        if (CLIConsole_avail() != 0) {
            break;
        }

        OS::Delay(1);
    }

#if LOG_MODULE_LEVEL == LOG_LEVEL_DEBUG
    uint32_t total = 0;
#endif

    buffSize /= 2;

    uint8_t* b1 = &buff[0];
    uint8_t* b2 = &buff[buffSize];
    uint8_t* b  = b1;

    uint16_t buffCount = 0;

    t.start(TIMER_SYS_MS_SECONDS(2));
    while (!t.expired()) {
        const uint16_t recv = CLIConsole_avail();
        
        if (recv == 0) {
            OS::Delay(1);
            continue;
        }

        uint16_t toRead = recv;

        const uint16_t availableSpace = buffSize - buffCount;

        if (toRead > availableSpace) {
            toRead = availableSpace;
        }

        toRead = CLIConsole_recv(&b[buffCount], toRead);

        LOG_DEBUG("recv=%d\n", (int)toRead);

        if (echoEnabled) {
            CLIConsole_send(&b[buffCount], toRead);
        }

        buffCount += toRead;

        if (buffCount >= buffSize) {
            buffCount = 0;

            if (!stream.write(b, buffSize)) {
                LOG_ERROR("failed to write to file\n");
                return;
            }

            if (b == b1) {
                b = b2;
            } else {
                b = b1;
            }

            #if LOG_MODULE_LEVEL == LOG_LEVEL_DEBUG
                total += buffSize;
                LOG_DEBUG("total=%d\n", (int)total);
            #endif
        }

        t.reset();
    }

    if (buffCount) {
        if (!stream.write(b, buffCount) != FR_OK) {
            LOG_ERROR("failed to write to file\n");
            return;
        }

        #if LOG_MODULE_LEVEL == LOG_LEVEL_DEBUG
            LOG_DEBUG("total=%d\n", (int)(total + buffCount));
        #endif
    }
}

static bool cat_isAbsoluteFatFsPath(const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        return false;
    }

    // /dir/file
    if (path[0] == '/') {
        return true;
    }

    // 0:/dir/file
    if (isdigit((unsigned char)path[0]) && path[1] == ':' && path[2] == '/') {
        return true;
    }

    return false;
}

static bool cat_buildPath(char* out, size_t outSize, const char* fileName)
{
    if (out == nullptr || outSize == 0 || fileName == nullptr || fileName[0] == '\0') {
        return false;
    }

    // it is an absolute path - just make a copy
    if (cat_isAbsoluteFatFsPath(fileName)) {
        return snprintf(out, outSize, "%s", fileName) < (int)outSize;
    }

    // ./file -> file
    if (fileName[0] == '.' && fileName[1] == '/') {
        fileName += 2;
    }

    if (f_getcwd(out, outSize) != FR_OK) {
        return false;
    }

    const size_t len = strlen(out);
    if (len == 0) {
        return false;
    }

    const bool needSlash = out[len - 1] != '/';

    const int written = snprintf(
        out + len,
        outSize - len,
        "%s%s",
        needSlash ? "/" : "",
        fileName
    );

    return written >= 0 && (size_t)written < (outSize - len);
}

static void cat_create(const char* fileName, bool echoEnabled)
{
    auto stream = std::make_unique<Sink::PushFileWriter>();
    
    char fn[FF_MAX_LFN];

    if (!cat_buildPath(fn, sizeof(fn), fileName)) {
        CLIConsole_print("failed to build file path\n");
        return;
    }

    if (!stream->start(fn, true)) {
        CLIConsole_print("failed to open the file\n");
        return;
    }

    static const uint16_t buffSize = FF_MAX_SS * 2;
    uint8_t* buff = (uint8_t*) malloc(buffSize);
    ASSERT(buff != NULL);

    cat_create(*stream, echoEnabled, buff, buffSize);

    free(buff);

    CLIConsole_print("\n");

    OS::Delay(500);
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void cat_run(int argc, const char** argv, bool echoEnabled)
{
    if (argc == 1) {
        CLIConsole_print(CLI_INVALID_PARAM_NUMBER_MSG);
        return;
    }

    CLIParser_t p;
    CLIParser_init(&p, argc, argv);

    if (argc >= 3) {
        if (strcmp(argv[1], ">") == 0) {
            echoEnabled = CLIParser_check(&p, "-e");
            cat_create(argv[2], echoEnabled);
            return;
        }
    }

    cat_t settings;
    cat_default(&settings);

    if (f_getcwd(settings.fileName, sizeof(settings.fileName)) != FR_OK) {
        CLIConsole_print("could not get current work directory\n");
        return;
    }

    if (settings.fileName[strlen(settings.fileName) - 1] != '/') {
        strcat(settings.fileName, "/");
    }

    strcat(settings.fileName, argv[1]);

    settings.modeBinary      = CLIParser_check(&p, "-bin");
    settings.showLineNumber  = CLIParser_check(&p, "-n");
    settings.modeBinaryASCII = CLIParser_check(&p, "-ascii");

    uint32_t modeBinaryWidth;
    if (CLIParser_getValueAsUI32(&p, "-c", &modeBinaryWidth)) {
        settings.modeBinaryWidth = modeBinaryWidth;
    }

    uint32_t pageLines;
    if (CLIParser_getValueAsUI32(&p, "-page", &pageLines)) {
        if (pageLines > 65535) {
            pageLines = 65535;
        }
        settings.pageLines = pageLines;
    }

    uint32_t offset;
    if (CLIParser_getValueAsUI32(&p, "-ofs", &offset)) {
        settings.offset = offset;
    }

    FIL* fd = (FIL*) malloc(sizeof(FIL));
    if (fd == NULL) {
        return;
    }
    
    FRESULT res = f_open(fd, settings.fileName, FA_READ);
    if (res != FR_OK) {
        LOG_ERROR("failed to open the file. Error code - (%d)\n", static_cast<int>(res));
        free(fd);
        return;
    }

    f_lseek(fd, settings.offset);

    if (settings.modeBinary == false) {
        cat_modeTxt(fd, &settings);
    } else {
        cat_modeBin(fd, &settings);
    }

    CLIConsole_print("\n");

    f_close(fd);
    free(fd);
}
