/**
 ******************************************************************************
 * @file    VibroStub.cpp
 * @brief   Vibro interface stub.
 *
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ff.h"
#include <string.h>

FRESULT f_chdir (const TCHAR* path)
{
    (void)path;

    return FR_OK;
}

FRESULT f_getcwd (TCHAR* buff, UINT len)
{
    static const TCHAR cwd[] = "0:/";
    const UINT needLen = (UINT)(sizeof(cwd)); /* includes '\0' */

    if (buff == 0) {
        return FR_INVALID_OBJECT;
    }

    if (len < needLen) {
        return FR_NOT_ENOUGH_CORE;
    }

    memcpy(buff, cwd, needLen);

    return FR_OK;
}
