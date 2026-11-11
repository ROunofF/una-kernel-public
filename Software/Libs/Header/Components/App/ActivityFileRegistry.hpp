/**
 ******************************************************************************
 * @file    ActivityFileRegistry.hpp
 * @brief   Persistent unique file path registry backed by a plain-text file.
 ******************************************************************************
 */

#ifndef __ACTIVITY_FILE_REGISTRY_HPP
#define __ACTIVITY_FILE_REGISTRY_HPP

#include <cstddef>
#include <vector>
#include <string_view>

#include "IFileSystem.hpp"
#include "FileSystemGuard.hpp"

/**
 * @brief   Persistent registry of unique file paths.
 *
 * Implements IFileEventListener to track file appearance and disappearance
 * events. The registry is backed by a plain-text file where each line contains
 * one path. On every modification the registry opens the file once in
 * read+write mode (without truncation), reads existing content, modifies the
 * in-memory line list, then seeks to 0, truncates, and rewrites the file.
 *
 * Filtering (e.g. by extension or subdirectory) is the caller's responsibility
 * and must be applied in onFileAppeared() / onFileDisappeared() before
 * delegating to add() / remove().
 */
class ActivityFileRegistry : public IFileEventListener {
public:

    /// Extra bytes reserved beyond the current file size on each read.
    static constexpr size_t kReadReserve = 256;

    /**
     * @brief   Constructor.
     * @param   fs:       File system used to access the registry file.
     * @param   filePath: Path to the backing text file (relative to fs root).
     */
    ActivityFileRegistry(Interface::IFileSystem &fs, const char *filePath);

    /**
     * @brief   Destructor.
     */
    virtual ~ActivityFileRegistry() = default;

    // -------------------------------------------------------------------------
    // IFileEventListener
    // -------------------------------------------------------------------------

    /**
     * @brief   Called when a file has appeared. Subclasses should filter the
     *          path before calling add().
     * @param   relativePath: Relative path of the appeared file.
     */
    virtual void onFileAppeared(const char *relativePath) override;

    /**
     * @brief   Called when a file has disappeared. Subclasses should filter
     *          the path before calling remove().
     * @param   relativePath: Relative path of the disappeared file.
     */
    virtual void onFileDisappeared(const char *relativePath) override;

    // -------------------------------------------------------------------------
    // Registry operations
    // -------------------------------------------------------------------------

    /**
     * @brief   Adds a path to the registry if it is not already present.
     *          Performs a full read-modify-write cycle on the backing file.
     * @param   path: Path string to add.
     * @retval  'true' on success, 'false' if the file could not be accessed
     *          or the path was already present.
     */
    bool add(const char *path);

    /**
     * @brief   Removes a path from the registry if present.
     *          Performs a full read-modify-write cycle on the backing file.
     * @param   path: Path string to remove.
     * @retval  'true' on success, 'false' if the file could not be accessed
     *          or the path was not found.
     */
    bool remove(const char *path);

private:

    /**
     * @brief   Reads the entire backing file into a heap buffer and parses
     *          each non-empty line into a string_view pointing into that buffer.
     * @param   outBuf:   Receives the raw file contents (sized file+reserve).
     * @param   outLines: Receives one string_view per non-empty line.
     * @param   file:     Open file object positioned at offset 0.
     * @retval  'true' on success, 'false' on read error or allocation failure.
     */
    bool readLines(std::vector<char> &outBuf,
                   std::vector<std::string_view> &outLines,
                   Interface::IFile &file);

    /**
     * @brief   Serializes all string_views in lines to the file, one per line.
     *          The caller must have truncated the file to 0 and seeked to 0
     *          before calling this method.
     * @param   lines: Lines to write.
     * @param   file:  Open writable file object.
     * @retval  'true' on success, 'false' on write error.
     */
    bool writeLines(const std::vector<std::string_view> &lines,
                    Interface::IFile &file);

    /// File system used to open the registry file.
    Interface::IFileSystem &mFs;

    /// Path to the backing text file.
    char mFilePath[Interface::IFileSystem::skMaxPathLen];
};

#endif /* __ACTIVITY_FILE_REGISTRY_HPP */
