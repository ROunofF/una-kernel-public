/**
 ******************************************************************************
 * @file    Folder.h
 * @brief   A set of basic tools for working with folders.
 *          
 ******************************************************************************
 *
 ******************************************************************************
 */

 #ifndef __Folder_h__
#define __Folder_h__

#ifdef __cplusplus
 extern "C" {
#endif

#include "Porting.h"

#include "ff.h"

#include <stdio.h>
#include <time.h>

////////////////////////////////////////////////////////////////////////////////
/// Data types
////////////////////////////////////////////////////////////////////////////////

typedef struct {
    char     name[FF_MAX_LFN];
    bool     isDir;
    bool     isHiden;
    bool     isSystem;
    uint32_t size;

    struct tm ts;
} FolderItem_t;

typedef struct {
    DIR     dir;
    FILINFO fileInfo;
} FolderRdCtx_t;

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

bool Folder_readingStart(FolderRdCtx_t* ctx, const char* name);
bool Folder_readingNext(FolderRdCtx_t* ctx, FolderItem_t* item);
void Folder_readingStop(FolderRdCtx_t* ctx);

bool  Folder_isExists(const char* folderName);
char* Folder_getDirName(const char* fileName);
void  Folder_create(const char* folderName);
bool  Folder_remove(const char* folderName);

#ifdef __cplusplus
 }
#endif

#endif
