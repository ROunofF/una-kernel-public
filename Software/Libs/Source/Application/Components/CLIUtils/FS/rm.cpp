#include "Components/CLIUtils/FS/rm.hpp"
#include "CLI/CLIConsole.h"
#include "CLI/CLIParser.h"
#include "FileSystem/File.hpp"
#include "FileSystem/Folder.h"

#include "ff.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private data
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
/// Private code
////////////////////////////////////////////////////////////////////////////////

static void rm_file(const char* fileName)
{
    char folder[FF_MAX_LFN];
    if (f_getcwd(folder, sizeof(folder)) != FR_OK) {
        CLIConsole_print("could not get current work directory\n");
        return;
    }

    int folderLength = strlen(folder);
    if (folder[folderLength - 1] != '/') {
        strcat(folder, "/");
    }

    strcat(folder, fileName);
    fileName = FS::File::getFileName(folder);
    folder[fileName - folder - 1] = '\0';

    FRESULT fr;     // Return value
    DIR dj;         // Directory object
    FILINFO fno;    // File information

    fr = f_findfirst(&dj, &fno, folder, fileName);      // Start to search for photo files

    while (fr == FR_OK && fno.fname[0]) {               // Repeat while an item is found
        char fullName[FF_MAX_LFN * 3];
        snprintf(fullName, sizeof(fullName), "%s/%s", folder, fno.fname);
        if (fno.fattrib == AM_DIR) {
            CLIConsole_print("DIR - %s\n", fullName);             // Print the object name
            Folder_remove(fullName);
        } else {
            CLIConsole_print("      %s\n", fullName);             // Print the object name
            FS::File::Remove(fullName);
        }
        fr = f_findnext(&dj, &fno);                     // Search for next item
    }

    f_closedir(&dj);
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

void rm_run(int argc, const char** argv)
{
    if (argc == 1) {
        CLIConsole_print(CLI_INVALID_PARAM_NUMBER_MSG);
        return;
    }

    char fileName[FF_MAX_LFN];
    if (f_getcwd(fileName, sizeof(fileName)) != FR_OK) {
        CLIConsole_print("could not get current work directory\n");
        return;
    }

    if (fileName[strlen(fileName) - 1] != '/') {
        strcat(fileName, "/");
    }

    strcat(fileName, argv[1]);

    rm_file(argv[1]);
}
