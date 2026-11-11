/**
 ******************************************************************************
 * @file    Folder.c
 * @brief   A set of basic tools for working with folders.
 *          
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Folder"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "FileSystem/Folder.h"

#include <string.h>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////////
// Private code
///////////////////////////////////////////////////////////////////////////////

// Adapted from the delete_node() sample in the FatFs documentation (f_unlink),
// copyright ChaN, under the FatFs licence.
static FRESULT delete_node(TCHAR*   path,       // Path name buffer with the sub-directory to delete
                           UINT     sz_buff,    // Size of path name buffer (items)
                           FILINFO* fno)        // Name read buffer
{
    UINT i, j;
    FRESULT fr;
    DIR dir;

    fr = f_opendir(&dir, path); /* Open the directory */
    if (fr != FR_OK) return fr;

    for (i = 0; path[i]; i++) ; /* Get current path length */
    path[i++] = _T('/');

    for (;;) {
        fr = f_readdir(&dir, fno);  /* Get a directory item */
        
        if (fr != FR_OK || !fno->fname[0]) {
            break;   /* End of directory? */
        }

        j = 0;
        
        do {    /* Make a path name */
            if (i + j >= sz_buff) { /* Buffer over flow? */
                fr = FR_INVALID_PARAMETER;
                break;
            }
            
            path[i + j] = fno->fname[j];
        } while (fno->fname[j++]);
        
        if (fno->fattrib & AM_DIR) {    /* Item is a directory */
            fr = delete_node(path, sz_buff, fno);
        }
        else {                        /* Item is a file */
            fr = f_unlink(path);
        }
        
        if (fr != FR_OK) {
            break;
        }
    }

    path[--i] = 0;  /* Restore the path name */
    
    f_closedir(&dir);

    if (fr == FR_OK) {
        fr = f_unlink(path);  /* Delete the empty directory */
    }
    
    return fr;
}

static char* cloneString(const char* str)
{
    char* s = (char*)malloc(strlen(str) + 1);

    if (s) {
       strcpy(s, str);
    }

    return s;
}

////////////////////////////////////////////////////////////////////////////////
/// Public code
////////////////////////////////////////////////////////////////////////////////

/**
 * @brief Starts to read specified folder
 * 
 * @details It must be called the firts. After that, you can use the Folder_readingStart
 *          function to extract one element from the specified directory.
 * 
 * @param ctx  Pointer to the FolderRdCtx_t object
 * @param name Folder name 
 * @return     True if successfully, otherwise false 
 */
bool Folder_readingStart(FolderRdCtx_t* ctx, const char* name)
{
    ASSERT(ctx != NULL);
    ASSERT(name != NULL);

    if (f_opendir(&ctx->dir, name) != FR_OK) {
        LOG_ERROR("failed to open the folder\n");
        return false;
    }

    return true;
}

/**
 * @brief Continues to read specified folder
 * 
 * @details Use this function only after calling Folder_readingStart
 * 
 * @param ctx  Pointer to the FolderRdCtx_t object
 * @param item Pointer to the FolderItem_t object
 * @return     True if successfully, otherwise false 
 */
bool Folder_readingNext(FolderRdCtx_t* ctx, FolderItem_t* item)
{
    for (;;) {
        if (f_readdir(&ctx->dir, &ctx->fileInfo) != FR_OK) {
            LOG_ERROR("failed to read the folder\n");
            return false;
        }
        
        if (ctx->fileInfo.fname[0] == '\0') {
            f_closedir(&ctx->dir);
            return false;
        }

        // if ((strcmp(ctx->fileInfo.fname, ".") == 0) || (strcmp(ctx->fileInfo.fname, "..") == 0)) {
        //     return true;
        // }

        strcpy(item->name, ctx->fileInfo.fname);
        // Attributes are a bitfield: test each bit independently so an entry
        // with more than one bit set (e.g. a hidden system directory, or any
        // file carrying the archive bit) is classified correctly. Mirrors
        // FileSystem::fillObjectInfo on the C++ side (this C path uses its own
        // FolderItem_t struct, so the helper cannot be shared directly).
        item->isDir    = (ctx->fileInfo.fattrib & AM_DIR) != 0;
        item->isHiden  = (ctx->fileInfo.fattrib & AM_HID) != 0;
        item->isSystem = (ctx->fileInfo.fattrib & AM_SYS) != 0;
        item->size     = ctx->fileInfo.fsize;

        item->ts.tm_hour = (ctx->fileInfo.ftime & 0xF800) >> 11;
        item->ts.tm_min  = (ctx->fileInfo.ftime & 0x7E0) >> 5;
        item->ts.tm_sec  = (ctx->fileInfo.ftime & 0x1F) * 2;
        item->ts.tm_mday = (ctx->fileInfo.fdate & 0x1F);
        item->ts.tm_mon  = ((ctx->fileInfo.fdate & 0x1E0) >> 5);
        item->ts.tm_year = ((ctx->fileInfo.fdate & 0xFE00) >> 9) - 20;

        return true;
    }
}

/**
 * @brief Stops reading specified folder
 * 
 * @details This function must be called after functions Folder_readingStart
 *          and Folder_readingNext have done their work and they will not be called again.
 * 
 * @param ctx Pointer to the FolderRdCtx_t object
 */
void Folder_readingStop(FolderRdCtx_t* ctx)
{
    f_closedir(&ctx->dir);
}

/**
 * @brief Checks if the folder exists
 * 
 * @param folderName Folder name
 * @return           True if exists, otherwise false 
 */
bool Folder_isExists(const char* folderName)
{
    DIR dir = {0};

    if (f_opendir(&dir, folderName) == FR_OK) {
        f_closedir(&dir);
        return true;
    }

    return false;
}

/**
 * @brief Returns only the folder name from the full path to the filename.
 * 
 * @warning Do not forget to call free function after using of the allocated buffer
 * 
 * @param  fileName File name
 * @return char*    Pointer to the folder name 
 */
char* Folder_getDirName(const char* fileName)
{
    char* slashPos = strrchr(fileName, '/');

    if (slashPos == NULL) {
        return NULL;
    }

    int len = slashPos - fileName;

    char* res = (char*) malloc(len + 1);

    memcpy(res, fileName, len);

    res[len] = '\0';

    return res;
}

/**
 * @brief Creates all folders in the specified path
 * 
 * @param folderName Folder name
 */
void Folder_create(const char* folderName)
{
    FRESULT res = f_mkdir(folderName);

    if (res == FR_NO_PATH) {
        char* s = cloneString(folderName);
        int   i = strlen(s);

        while(i && s[i-1] != '/') // Find decomposition point
            i--;

        if (i) // Move to '/'
            i--;

        if (i) {
            s[i] = 0; // replace '/' with NUL
            Folder_create(s); // Drop down a level, and build that
            res = f_mkdir(folderName); // Try again
            if (res != FR_OK) {
                LOG_ERROR("unable to create a folder\n");
            }
        }

        free(s);
    }
}

/**
 * @brief Removes folders recursively
 * 
 * @param folderName Folder name
 * @return           True if successfully, otherwise false 
 */
bool Folder_remove(const char* folderName)
{
    char buff[FF_MAX_LFN];

    strcpy(buff, folderName);

    FILINFO fno;

    if (delete_node(buff, ARRAY_SIZE(buff), &fno) != FR_OK) {
        LOG_ERROR("failed to remove the folder\n");
        return false;
    }

    return true;
}
