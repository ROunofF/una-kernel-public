/**
 ******************************************************************************
 * @file    FileSystemGuard.hpp
 * @brief   File system wrapper with access restrictions for user applications.
 ******************************************************************************
 */

#ifndef __FILE_SYSTEM_GUARD_HPP
#define __FILE_SYSTEM_GUARD_HPP

#include <cstdint>
#include <cstdbool>
#include <memory>

#include "FileSystem.hpp"
#include "File.hpp"
#include "Directory.hpp"
#include "GuardPath.hpp"

/**
 * @brief   Interface for receiving file appearance and disappearance events.
 *
 * Implement this interface and register it via FileSystemGuard::setFileEventListener()
 * to be notified when a file is created, modified, renamed, or removed.
 *
 * A file is considered "appeared" when:
 *   - it has been written to and closed, or
 *   - it has been renamed/moved into the guard root scope.
 *
 * A file is considered "disappeared" when:
 *   - it has been removed, or
 *   - it has been renamed/moved out of the guard root scope.
 *
 * @note    Path filtering (e.g. by extension or subdirectory) is the
 *          responsibility of the listener implementation.
 */
class IFileEventListener {
public:
    virtual ~IFileEventListener() = default;

    /**
     * @brief   Called when a file has appeared: written and closed, or
     *          renamed into the monitored scope.
     * @param   relativePath: Relative path of the file within the guard root.
     */
    virtual void onFileAppeared(const char *relativePath) = 0;

    /**
     * @brief   Called when a file has disappeared: removed, or renamed out
     *          of the monitored scope.
     * @param   relativePath: Relative path of the file within the guard root.
     */
    virtual void onFileDisappeared(const char *relativePath) = 0;
};

/**
 * @brief   A wrapper for the file system providing access control for user
 *          applications.
 *
 * This class manages the creation of files and directories with restricted
 * access to the root directory of the application.
 */
class FileSystemGuard : public Interface::IFileSystem {
public:
    /**
     * @brief   Constructor.
     * @param   realfs: Reference to the underlying real file system.
     */
    FileSystemGuard(Interface::IFileSystem &realfs) :
            mRealFs(realfs) {}

    /**
     * @brief   Constructor with root path.
     * @param   realfs:   Reference to the underlying real file system.
     * @param   rootPath: Root directory path to restrict access to.
     */
    FileSystemGuard(Interface::IFileSystem &realfs, const char *rootPath) :
            mRealFs(realfs)
    {
        setRootDirPath(rootPath);
    }

    /**
     * @brief   Destructor.
     */
    virtual ~FileSystemGuard() = default;

    /**
     * @brief   Sets the base location path for the root directory.
     * @param   rootPath: The base path for the root directory.
     */
    void setRootDirPath(const char *rootPath);

    /**
     * @brief   Lets ":/<name>" paths reach the read-only folders in
     *          FsGuard::kVolumeRoots (":/Workouts" is "<volume>:/Workouts").
     *          For app guards only; off by default, so other guards keep
     *          treating ":/" as an ordinary (and invalid) relative path.
     */
    void enableVolumeRoots(bool enable) { mVolumeRoots = enable; }

    /**
     * @brief   Constructs an absolute path by prepending the root directory path.
     * @param   buffer:       Output buffer to store the resulting full path.
     * @param   buffSize:     Size of the output buffer in bytes.
     * @param   relativePath: Relative path to append to the root path.
     * @param   access:       If not null, set to what the path may be used for.
     * @retval  'true' if the full path was successfully constructed, 'false' if
     *          the path is rejected or exceeds the buffer size.
     */
    bool getFullPath(char *buffer, size_t buffSize, const char *relativePath,
                     FsGuard::Access *access = nullptr) const;

    /**
     * @brief   Writes the relative form of a full path (root prefix stripped,
     *          or the ../SharedData/ form for shared-data paths) into @p out.
     * @param   fullPath: Full absolute path string.
     * @param   out:      Caller-owned output buffer (no shared/aliased storage).
     * @param   outSize:  Size of @p out in bytes.
     * @retval  'true' on success, 'false' if inputs are invalid or truncated.
     */
    bool getRelativePath(const char *fullPath, char *out, size_t outSize) const;

    /**
     * @brief   Registers a listener to receive file events.
     * @param   listener: Pointer to the listener, or nullptr to unregister.
     */
    void setFileEventListener(IFileEventListener *listener);

    /**
     * @brief   Notifies the registered listener that a file has appeared
     *          (written+closed, or renamed in).
     * @param   relativePath: Relative path of the file.
     */
    void notifyFileAppeared(const char *relativePath);

    /**
     * @brief   Notifies the registered listener that a file has disappeared
     *          (removed, or renamed out).
     * @param   relativePath: Relative path of the file.
     */
    void notifyFileDisappeared(const char *relativePath);

    /**
     * @brief   Creates a directory at the given relative path.
     * @param   path: Relative path of the directory to create.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool mkdir(const char *path) override;

    /**
     * @brief   Creates a file object at the given relative path.
     * @param   path: Relative path of the file.
     * @retval  Unique pointer to the IFile object, or nullptr on allocation failure.
     */
    virtual std::unique_ptr<Interface::IFile> file(const char *path) override;

    /**
     * @brief   Creates a directory object at the given relative path.
     * @param   path: Relative path of the directory.
     * @retval  Unique pointer to the IDirectory object, or nullptr on allocation failure.
     */
    virtual std::unique_ptr<Interface::IDirectory> dir(const char *path) override;

    /**
     * @brief   Checks whether an object exists at the given relative path.
     * @param   path: Relative path to check.
     * @retval  'true' if the object exists, 'false' otherwise.
     */
    virtual bool exist(const char *path) const override;

    /**
     * @brief   Removes the object at the given relative path.
     * @param   path: Relative path of the object to remove.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool remove(const char *path) override;

    /**
     * @brief   Renames or moves an object within the guard root.
     * @param   oldPath: Relative path of the existing object.
     * @param   newPath: Relative path of the destination.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool rename(const char *oldPath, const char *newPath) override;

    /**
     * @brief   Copies an object within the guard root.
     * @param   oldPath: Relative path of the source object.
     * @param   newPath: Relative path of the destination.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool copy(const char *oldPath, const char *newPath) override;

    /**
     * @brief   Retrieves metadata for the object at the given relative path.
     * @param   path: Relative path of the object.
     * @param   item: Output structure to fill with object metadata.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool objectInfo(const char *path, ObjectInfo &item) const
            override;

private:

    // Real FS object reference
    Interface::IFileSystem &mRealFs;

    /// Base path for the file system operations.
    char mpRootPath[Interface::IFileSystem::skMaxPathLen] {};

    /// Optional listener for file write events.
    IFileEventListener *mFileEventListener { nullptr };

    /// Whether ":/" paths name volume-root folders (see enableVolumeRoots).
    bool mVolumeRoots { false };

    /// Resolve @p path for an operation that modifies it. Logs and returns
    /// false if the path is rejected or read-only.
    bool getModifiablePath(char *buffer, size_t buffSize, const char *path) const;

    friend class FileGuard;
    friend class DirectoryGuard;
};

/**
 * @brief   File object with additional location path management.
 *
 * This class extends basic file operations and ensures the file paths are
 * relative to the set base location. Tracks write operations and notifies
 * the parent FileSystemGuard when a written file is closed.
 */
class FileGuard : public FS::File {
public:
    /**
     * @brief   Constructor.
     * @param   relativePath: The relative path of the file.
     * @param   fs: Reference to the parent filesystem.
     */
    FileGuard(const char *relativePath, FileSystemGuard &fs);

    /**
     * @brief   Destructor. Closes the file if it is still open.
     */
    ~FileGuard() = default;

    /**
     * @brief   Redirects the file object to a new relative path.
     *          Closes the file first if it is currently open.
     * @param   relativePath: New relative path of the file.
     */
    virtual void setPath(const char *relativePath) override;

    /**
     * @brief   Returns the relative path of the file.
     * @retval  Relative path string within the guard root.
     */
    virtual const char* getPath() const override;

    /**
     * @brief   Renames the file to a new relative path. Notifies the parent
     *          FileSystemGuard that the old path has disappeared and the new
     *          path has appeared.
     * @param   newPath: Relative destination path.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool rename(const char *newPath) override;

    /**
     * @brief   Removes the file. Notifies the parent FileSystemGuard that
     *          the file has disappeared.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool remove() override;

    /**
     * @brief   Opens the file. Refused in write mode on a read-only path.
     */
    virtual bool open(bool wMode = false, bool override = false) override;

    /**
     * @brief   Truncates the file. Refused on a read-only path.
     */
    virtual bool truncate(size_t offset) override;

    /**
     * @brief   Writes data to the file. Marks the file as having been written.
     * @param   buff: Buffer containing the data to write.
     * @param   btw: Number of bytes to write.
     * @param   bw: Reference to store the number of bytes actually written.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool write(const char *buff, size_t btw, size_t &bw) override;

    /**
     * @brief   Closes the file. If the file was written to, notifies the
     *          parent FileSystemGuard.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool close() override;

private:

    /// Reference to the parent file system.
    FileSystemGuard &mFs;

    /// What the path may be used for.
    FsGuard::Access mAccess { FsGuard::Access::App };

    /// True if at least one successful write() call was made.
    bool mWasWritten { false };

    /// Per-object buffer backing getPath() (avoids aliasing shared storage).
    mutable char mRelPath[Interface::IFileSystem::skMaxPathLen] {};
};

/**
 * @brief   Directory object with additional location path management.
 *
 * This class extends basic directory operations and ensures the directory
 * paths are relative to the set base location.
 */
class DirectoryGuard : public FS::Directory {
public:
    /**
     * @brief   Constructor.
     * @param   relativePath: The relative path of the directory.
     * @param   fs: Reference to the parent filesystem.
     */
    DirectoryGuard(const char *relativePath, FileSystemGuard &fs);

    /**
     * @brief   Destructor.
     */
    ~DirectoryGuard() = default;

    /**
     * @brief   Redirects the directory object to a new relative path.
     *          Closes the directory first if it is currently open.
     * @param   relativePath: New relative path of the directory.
     */
    virtual void setPath(const char *relativePath) override;

    /**
     * @brief   Returns the relative path of the directory.
     * @retval  Relative path string within the guard root.
     */
    virtual const char* getPath() const override;

    /**
     * @brief   Renames the directory to a new relative path.
     * @param   newPath: Relative destination path.
     * @retval  'true' on success, 'false' otherwise.
     */
    virtual bool rename(const char *newPath) override;

    /**
     * @brief   Creates the directory. Refused on a read-only path.
     *          FS::Directory::create() would otherwise reach the real file
     *          system without passing the guard.
     */
    virtual bool create() override;

    /**
     * @brief   Removes the directory. Refused on a read-only path, for the
     *          same reason as create().
     */
    virtual bool remove() override;

private:

    /// Reference to the parent file system.
    FileSystemGuard &mFs;

    /// What the path may be used for.
    FsGuard::Access mAccess { FsGuard::Access::App };

    /// Per-object buffer backing getPath() (avoids aliasing shared storage).
    mutable char mRelPath[Interface::IFileSystem::skMaxPathLen] {};
};

#endif /* __FILE_SYSTEM_GUARD_HPP */
