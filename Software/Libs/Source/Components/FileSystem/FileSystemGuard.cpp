/**
 ******************************************************************************
 * @file    FileSystemGuard.cpp
 * @brief   File system wrapper with access restriction for user applications.
 ******************************************************************************
 */

#include "FileSystemGuard.hpp"
#include "GuardPath.hpp"

#define LOG_MODULE_PRX      "FileSystemGuard"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include <cassert>
#include <cstring>

// ---------------------------------------------------------------------------
// FileSystemGuard
// ---------------------------------------------------------------------------

void FileSystemGuard::setRootDirPath(const char *rootPath)
{
    assert(rootPath != nullptr);
    size_t pathLen = strlen(rootPath);
    assert(pathLen < Interface::IFileSystem::skMaxPathLen);

    snprintf(mpRootPath, sizeof(mpRootPath), "%s", rootPath);

    // Remove trailing '/' if any
    if (pathLen > 0 && mpRootPath[pathLen - 1] == '/') {
        mpRootPath[pathLen - 1] = '\0';
    }
}

bool FileSystemGuard::getFullPath(char *buffer, size_t buffSize, const char *relativePath,
                                  FsGuard::Access *access) const
{
    // Path policy (SharedData and volume-root whitelists, traversal rejection)
    // lives in the pure, host-tested FsGuard helper; see GuardPath.hpp.
    FsGuard::Access a = FsGuard::Access::App;
    const bool ok = FsGuard::resolve(mpRootPath, relativePath, mVolumeRoots,
                                     buffer, buffSize, a);
    if (!ok) {
        LOG_ERROR("Rejected/invalid path! [%s]\n",
                  relativePath != nullptr ? relativePath : "(null)");
    }
    if (access != nullptr) {
        *access = a;
    }
    return ok;
}

bool FileSystemGuard::getModifiablePath(char *buffer, size_t buffSize, const char *path) const
{
    FsGuard::Access access = FsGuard::Access::App;
    if (!getFullPath(buffer, buffSize, path, &access)) {
        return false;
    }
    if (!FsGuard::canModify(access)) {
        LOG_ERROR("Read-only path! [%s]\n", path);
        return false;
    }
    return true;
}

bool FileSystemGuard::getRelativePath(const char *fullPath, char *out,
        size_t outSize) const
{
    // Inverse mapping (root-strip with boundary check, or the ../SharedData/
    // and :/ forms) lives in the pure, host-tested FsGuard helper (GuardPath.hpp).
    const bool ok = FsGuard::buildRelativePath(mpRootPath, fullPath, out, outSize,
                                               mVolumeRoots);
    if (!ok) {
        LOG_ERROR("The path is invalid!\n");
    }
    return ok;
}

void FileSystemGuard::setFileEventListener(IFileEventListener *listener)
{
    mFileEventListener = listener;
}

void FileSystemGuard::notifyFileAppeared(const char *relativePath)
{
    if (mFileEventListener != nullptr) {
        mFileEventListener->onFileAppeared(relativePath);
    }
}

void FileSystemGuard::notifyFileDisappeared(const char *relativePath)
{
    if (mFileEventListener != nullptr) {
        mFileEventListener->onFileDisappeared(relativePath);
    }
}

bool FileSystemGuard::mkdir(const char *path)
{
    assert(path != nullptr);

    char buff[Interface::IFileSystem::skMaxPathLen] {};

    if (!getModifiablePath(buff, sizeof(buff), path)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    return mRealFs.mkdir(buff);
}

std::unique_ptr<Interface::IFile> FileSystemGuard::file(const char *path)
{
    assert(path != nullptr);

    std::unique_ptr<FileGuard> ptr {new (std::nothrow) FileGuard(path, *this)};
    if (ptr == nullptr) {
        LOG_ERROR("No memory!\n");
    }
    return ptr;
}

std::unique_ptr<Interface::IDirectory> FileSystemGuard::dir(const char *path)
{
    assert(path != nullptr);

    std::unique_ptr<DirectoryGuard> ptr {new (std::nothrow) DirectoryGuard(path, *this)};
    if (ptr == nullptr) {
        LOG_ERROR("No memory!\n");
    }
    return ptr;
}

bool FileSystemGuard::remove(const char *path)
{
    assert(path != nullptr);

    char buff[Interface::IFileSystem::skMaxPathLen] {};

    if (!getModifiablePath(buff, sizeof(buff), path)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    bool result = mRealFs.remove(buff);

    if (result) {
        notifyFileDisappeared(path);
    }

    return result;
}

bool FileSystemGuard::rename(const char *oldPath, const char *newPath)
{
    assert(oldPath != nullptr);
    assert(newPath != nullptr);

    char buffOld[Interface::IFileSystem::skMaxPathLen] {};
    if (!getModifiablePath(buffOld, sizeof(buffOld), oldPath)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    char buffNew[Interface::IFileSystem::skMaxPathLen] {};
    if (!getModifiablePath(buffNew, sizeof(buffNew), newPath)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    bool result = mRealFs.rename(buffOld, buffNew);

    if (result) {
        notifyFileDisappeared(oldPath);
        notifyFileAppeared(newPath);
    }

    return result;
}

bool FileSystemGuard::copy(const char *oldPath, const char *newPath)
{
    assert(oldPath != nullptr);
    assert(newPath != nullptr);

    char buffOld[Interface::IFileSystem::skMaxPathLen] {};
    if (!getFullPath(buffOld, sizeof(buffOld), oldPath)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    // Copying out of a read-only folder is fine; copying into one is not.
    char buffNew[Interface::IFileSystem::skMaxPathLen] {};
    if (!getModifiablePath(buffNew, sizeof(buffNew), newPath)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    return mRealFs.copy(buffOld, buffNew);
}

bool FileSystemGuard::exist(const char *path) const
{
    assert(path != nullptr);

    char buff[Interface::IFileSystem::skMaxPathLen] {};

    if (!getFullPath(buff, sizeof(buff), path)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    return mRealFs.exist(buff);
}

bool FileSystemGuard::objectInfo(const char *path, ObjectInfo &item) const
{
    assert(path != nullptr);

    char buff[Interface::IFileSystem::skMaxPathLen] {};

    if (!getFullPath(buff, sizeof(buff), path)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    return mRealFs.objectInfo(buff, item);
}

// ---------------------------------------------------------------------------
// FileGuard
// ---------------------------------------------------------------------------

FileGuard::FileGuard(const char *relativePath, FileSystemGuard &fs) :
        mFs(fs)
{
    assert(relativePath != nullptr);
    // A rejected (e.g. traversal) or oversized path must not leave the guard
    // bound to a stale/garbage mPath; clear it so later FS ops fail fast.
    if (!mFs.getFullPath(mPath, sizeof(mPath), relativePath, &mAccess)) {
        mPath[0] = '\0';
        LOG_ERROR("FileGuard: rejected path '%s'\n", relativePath);
    }
}

void FileGuard::setPath(const char *relativePath)
{
    assert(relativePath != nullptr);

    if (isOpen()) {
        close();
    }

    mWasWritten = false;

    if (!mFs.getFullPath(mPath, sizeof(mPath), relativePath, &mAccess)) {
        mPath[0] = '\0';
        LOG_ERROR("FileGuard: rejected path '%s'\n", relativePath);
    }
}

bool FileGuard::open(bool wMode, bool override)
{
    if (wMode && !FsGuard::canModify(mAccess)) {
        LOG_ERROR("FileGuard: read-only, cannot open for writing '%s'\n", mPath);
        return false;
    }
    return FS::File::open(wMode, override);
}

bool FileGuard::truncate(size_t offset)
{
    if (!FsGuard::canModify(mAccess)) {
        LOG_ERROR("FileGuard: read-only, cannot truncate '%s'\n", mPath);
        return false;
    }
    return FS::File::truncate(offset);
}

const char* FileGuard::getPath() const
{
    return mFs.getRelativePath(mPath, mRelPath, sizeof(mRelPath)) ? mRelPath
                                                                  : nullptr;
}

bool FileGuard::rename(const char *newPath)
{
    assert(newPath != nullptr);

    if (!FsGuard::canModify(mAccess)) {
        LOG_ERROR("FileGuard: read-only, cannot rename '%s'\n", mPath);
        return false;
    }

    const char *oldRelPath = getPath();

    char buff[Interface::IFileSystem::skMaxPathLen] {};
    if (!mFs.getModifiablePath(buff, sizeof(buff), newPath)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    bool result = FS::File::rename(buff);

    if (result && oldRelPath != nullptr) {
        mFs.notifyFileDisappeared(oldRelPath);
        mFs.notifyFileAppeared(newPath);
    }

    return result;
}

bool FileGuard::remove()
{
    if (!FsGuard::canModify(mAccess)) {
        LOG_ERROR("FileGuard: read-only, cannot remove '%s'\n", mPath);
        return false;
    }

    const char *relPath = getPath();

    bool result = FS::File::remove();

    if (result && relPath != nullptr) {
        mFs.notifyFileDisappeared(relPath);
    }

    return result;
}

bool FileGuard::write(const char *buff, size_t btw, size_t &bw)
{
    // open() already refuses write mode on a read-only path; this keeps a
    // read-only file unwritable whatever state it is in.
    if (!FsGuard::canModify(mAccess)) {
        bw = 0;
        return false;
    }

    bool result = FS::File::write(buff, btw, bw);

    if (result && bw > 0) {
        mWasWritten = true;
    }

    return result;
}

bool FileGuard::close()
{
    bool shouldNotify = mWasWritten;
    const char *relPath = getPath();

    bool result = FS::File::close();

    if (result && shouldNotify && relPath != nullptr) {
        mFs.notifyFileAppeared(relPath);
    }

    mWasWritten = false;

    return result;
}

// ---------------------------------------------------------------------------
// DirectoryGuard
// ---------------------------------------------------------------------------

DirectoryGuard::DirectoryGuard(const char *relativePath,
        FileSystemGuard &fs) :
        mFs(fs)
{
    assert(relativePath != nullptr);
    // A rejected (e.g. traversal) or oversized path must not leave the guard
    // bound to a stale/garbage mPath; clear it so later FS ops fail fast.
    if (!mFs.getFullPath(mPath, sizeof(mPath), relativePath, &mAccess)) {
        mPath[0] = '\0';
        LOG_ERROR("DirectoryGuard: rejected path '%s'\n", relativePath);
    }
}

void DirectoryGuard::setPath(const char *relativePath)
{
    assert(relativePath != nullptr);

    if (isOpen()) {
        close();
    }

    if (!mFs.getFullPath(mPath, sizeof(mPath), relativePath, &mAccess)) {
        mPath[0] = '\0';
        LOG_ERROR("DirectoryGuard: rejected path '%s'\n", relativePath);
    }
}

bool DirectoryGuard::create()
{
    if (!FsGuard::canModify(mAccess)) {
        LOG_ERROR("DirectoryGuard: read-only, cannot create '%s'\n", mPath);
        return false;
    }
    return FS::Directory::create();
}

bool DirectoryGuard::remove()
{
    if (!FsGuard::canModify(mAccess)) {
        LOG_ERROR("DirectoryGuard: read-only, cannot remove '%s'\n", mPath);
        return false;
    }
    return FS::Directory::remove();
}

const char* DirectoryGuard::getPath() const
{
    return mFs.getRelativePath(mPath, mRelPath, sizeof(mRelPath)) ? mRelPath
                                                                  : nullptr;
}

bool DirectoryGuard::rename(const char *newPath)
{
    assert(newPath != nullptr);

    if (!FsGuard::canModify(mAccess)) {
        LOG_ERROR("DirectoryGuard: read-only, cannot rename '%s'\n", mPath);
        return false;
    }

    char buff[Interface::IFileSystem::skMaxPathLen] {};
    if (!mFs.getModifiablePath(buff, sizeof(buff), newPath)) {
        LOG_ERROR("Failed to create path\n");
        return false;
    }

    return FS::Directory::rename(buff);
}
