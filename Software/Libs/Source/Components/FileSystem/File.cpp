/**
 ******************************************************************************
 * @file    File.cpp
 * @brief   File object.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "File.hpp"

#include <cstdio>
#include <cstring>
#include <cassert>

#define LOG_MODULE_PRX      "File"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include "FileSystem.hpp"

namespace FS
{

File::File(const char *path)
{
    assert(path != nullptr);
    setPath(path);
}

File::~File()
{
    // Destroying a still-open File used to leak one of the FF_FS_LOCK sharing
    // slots (there are only 10) and drop whatever had not been flushed yet.
    // close() is a no-op when the file was never opened. The call is qualified
    // because a base destructor cannot dispatch to a derived override anyway,
    // and naming File::close() makes that explicit rather than surprising.
    File::close();
}

void File::setPath(const char *path)
{
    assert(path != nullptr);

    if (isOpen()) {
        close();
    }

    strncpy(mPath, path, sizeof(mPath));
}

const char* File::getPath() const
{
    return mPath;
}

bool File::exist() const
{
    return Exist(mPath);
}

bool File::rename(const char *newPath)
{
    if (isOpen()) {
        close();
    }

    if (Rename(mPath, newPath)) {
        setPath(newPath);
        return true;
    }

    return false;
}

bool File::remove()
{
    if (mIsOpened) {
        close();
    }

    return Remove(mPath);
}

size_t File::size() const
{
    return Size(mPath);
}

bool File::open(bool wMode, bool override)
{
    FRESULT fr = FR_DISK_ERR;

    if (override) {
        fr = f_open(&mFile, mPath,
                wMode ? FA_WRITE | FA_READ | FA_CREATE_ALWAYS : FA_READ);
    } else {
        fr = f_open(&mFile, mPath,
                wMode ? FA_WRITE | FA_READ | FA_OPEN_ALWAYS : FA_READ);
    }

    if (fr != FR_OK) {
        memset(&mFile, 0, sizeof(mFile));
        LOG_ERROR("Error opening file '%s' Err [%s]\n", mPath, ERRSTR(fr));
        return false;
    }

    mIsOpened = true;
    return true;
}

bool File::isOpen() const
{
    return mIsOpened;
}

bool File::close()
{
    if (!mIsOpened) {
        return true;
    }

    FRESULT fr = FR_DISK_ERR;

    fr = f_close(&mFile);

    if (fr != FR_OK) {
        LOG_ERROR("Error closing file '%s' Err [%s]\n", mPath, ERRSTR(fr));
        return false;
    }

    mIsOpened = false;

    memset(&mFile, 0, sizeof(mFile));

    return true;
}

bool File::read(char *buff, size_t btr, size_t &br)
{
    assert(buff != nullptr);

    FRESULT fr = FR_DISK_ERR;

    fr = f_read(&mFile, buff, (UINT) btr, (UINT*) &br);

    if (fr != FR_OK) {
        LOG_ERROR("Err [%s]\n", ERRSTR(fr));
        return false;
    }

    return true;
}

bool File::write(const char *buff, size_t btw, size_t &bw)
{
    assert(buff != nullptr);

    FRESULT fr = FR_DISK_ERR;

    fr = f_write(&mFile, buff, (UINT) btw, (UINT*) &bw);

    if (fr != FR_OK || btw != bw) {
        LOG_ERROR("Err [%s]\n", ERRSTR(fr));
        return false;
    }

    return true;
}

bool File::seek(size_t offset)
{
    FRESULT fr = FR_DISK_ERR;

    fr = f_lseek(&mFile, (UINT) offset);

    if (fr != FR_OK || mFile.fptr != offset) {
        LOG_ERROR("Err [%s]\n", ERRSTR(fr));
        return false;
    }

    return true;
}

bool File::truncate(size_t offset)
{
    FRESULT fr = FR_DISK_ERR;

    if (!seek(offset)) {
        return false;
    }

    fr = f_truncate(&mFile);

    if (fr != FR_OK) {
        LOG_ERROR("Err [%s]\n", ERRSTR(fr));
        return false;
    }

    return true;
}

bool File::flush()
{
    FRESULT fr = FR_DISK_ERR;

    fr = f_sync(&mFile);

    if (fr != FR_OK) {
        LOG_ERROR("Err [%s]\n", ERRSTR(fr));
        return false;
    }

    return true;
}

size_t File::getPosition() const
{
    return f_tell(&mFile);
}

bool File::Exist(const char *path)
{
    assert(path != nullptr);
    return FileSystem::GetInstance().exist(path);
}

bool File::Rename(const char *oldPath, const char *newPath)
{
    assert(oldPath != nullptr);
    assert(newPath != nullptr);

    return FileSystem::GetInstance().rename(oldPath, newPath);
}

bool File::Remove(const char *path)
{
    assert(path != nullptr);
    return FileSystem::GetInstance().remove(path);
}

size_t File::Size(const char *path)
{
    assert(path != nullptr);

    FRESULT fr = FR_DISK_ERR;
    FILINFO fno { };

    fr = f_stat(path, &fno);

    if (fr == FR_OK) {
        return fno.fsize;
    }

    LOG_ERROR("Err [%s]\n", ERRSTR(fr));

    return 0;
}

const char* File::getFileName(const char* path)
{
    const char *slash = strrchr(path, '/');

    if (slash) {
        return slash + 1;
    } else {
        return path;
    }
}

} /* namespace FS */
