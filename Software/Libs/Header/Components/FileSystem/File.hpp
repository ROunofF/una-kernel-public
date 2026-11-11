/**
 ******************************************************************************
 * @file    File.hpp
 * @brief   File object.
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __FILE_HPP
#define __FILE_HPP

#include <stdint.h>
#include <stdbool.h>

#include "IFileSystem.hpp"
#include "ff.h"

namespace FS
{

/**
 * @brief   File object.
 */
class File : public Interface::IFile {

public:

    /**
     * @brief   Creates a new File instance.
     */
    File() = default;

    /**
     * @brief   Creates a new File instance.
     * @param   path: Absolute path like '0:/dirname/filename'.
     */
    File(const char *path);

    /**
     * @brief   Destructor. Releases the FatFs handle if the file is still open.
     * @note    Only the handle is released. A derived close() override does not
     *          run from here, because the vtable is already File's by the time
     *          a base destructor executes.
     */
    virtual ~File();

    /// @name Non-copyable / non-movable
    /// An open FIL holds an index (obj.lockid) into FatFs' table of open
    /// objects, and it owns its own cached sector plus that sector's dirty
    /// flag. Copying the object duplicates both. Now that the destructor
    /// closes, two destructors would decrement one table entry twice — and
    /// that entry may by then have been reused for a different open file,
    /// silently dropping its write/delete exclusion — while the two cached
    /// sectors flush over each other. Nothing copies a File today; this keeps
    /// it that way by construction.
    /// @{
    File(const File&)            = delete;
    File& operator=(const File&) = delete;
    File(File&&)                 = delete;
    File& operator=(File&&)      = delete;
    /// @}

    /**
     * @brief   Redirect object to new path.
     * @note    This method will close file if it was open before.
     * @param   path: New path to file.
     */
    virtual void setPath(const char *path) override;

    /**
     * @brief   Returns the file path.
     * @retval  path: Current file path.
     */
    virtual const char* getPath() const override;

    /**
     * @brief   Checks if the file exists.
     * @retval  'true' if the file exists, 'false' otherwise.
     */
    virtual bool exist() const override;

    /**
     * @brief   Rename file.
     * @param   newPath: New file path.
     * @retval  'true' if the file was successfully renamed, 'false' otherwise.
     */
    virtual bool rename(const char *newPath) override;

    /**
     * @brief   Remove file.
     * @retval  'true' if the object was successfully removed, 'false' otherwise.
     */
    virtual bool remove() override;

    /**
     * @brief   Returns the size of the file in bytes.
     * @retval  File size in bytes.
     */
    virtual size_t size() const override;

    /**
     * @brief   Opens the file.. Creates a new, empty file if a file with this
     *          name does not yet exist.
     * @param   wMode: 'true' - if write mode, 'false' - read mode (default).
     * @param   override: 'true' file will be overridden. (FA_CREATE_ALWAYS),
     *          'false' - open (FA_OPEN_ALWAYS).
     * @retval  Execution status. 'true' - if success, 'false' - otherwise.
     */
    virtual bool open(bool wMode = false, bool override = false) override;

    /**
     * @brief   Checks if the file is open.
     * @retval  'true' if the file is open, 'false' otherwise.
     */
    virtual bool isOpen() const override;

    /**
     * @brief   Closes the file.
     * @retval  'true' if the file was successfully closed, 'false' otherwise.
     */
    virtual bool close() override;

    /**
     * @brief   Reads data from the file.
     * @param   buff: Buffer to store the read data.
     * @param   btr: Number of bytes to read.
     * @param   br: Reference to store the number of bytes actually read.
     * @retval  'true' if the read was successful, 'false' otherwise.
     */
    virtual bool read(char *buff, size_t btr, size_t &br) override;

    /**
     * @brief   Writes data to the file.
     * @param   buff: Buffer containing the data to write.
     * @param   btw: Number of bytes to write.
     * @param   bw: Reference to store the number of bytes actually written.
     * @retval  'true' if the write was successful, 'false' otherwise.
     */
    virtual bool write(const char *buff, size_t btw, size_t &bw) override;

    /**
     * @brief   Sets the current file position.
     * @param   offset: New position in bytes.
     * @retval  'true' if the position was successfully set, 'false' otherwise.
     */
    virtual bool seek(size_t offset) override;

    /**
     * @brief   Truncates the file at the given offset.
     * @param   offset: Position to truncate the file.
     * @retval  'true' if the file was successfully truncated, 'false' otherwise.
     */
    virtual bool truncate(size_t offset) override;

    /**
     * @brief   Synchronizes the file's data to the storage medium.
     * @retval  'true' if the sync was successful, 'false' otherwise.
     */
    virtual bool flush() override;

    /**
     * @brief   Returns the current position in the file.
     * @retval  Current file position in bytes.
     */
    virtual size_t getPosition() const override;

    /**
     * @brief   Checks if the file exists.
     * @param   path: Path to the file.
     * @retval  'true' if the file exists, 'false' otherwise.
     */
    static bool Exist(const char *path);

    /**
     * @brief   Rename file.
     * @param   oldPath: Current path of the file.
     * @param   newPath: New file path.
     * @retval  'true' if the file was successfully renamed, 'false' otherwise.
     */
    static bool Rename(const char *oldPath, const char *newPath);

    /**
     * @brief   Remove file.
     * @param   path: Path to the file.
     * @retval  'true' if the file was successfully removed, 'false' otherwise.
     */
    static bool Remove(const char *path);

    /**
     * @brief   Returns the size of the file in bytes.
     * @param   path: Path to the file.
     * @retval  File size in bytes.
     */
    static size_t Size(const char *path);

    static const char* getFileName(const char* path);

protected:

    // Path to the file
    char mPath[FF_MAX_LFN + 1] { };

    // Flag indicates that file is opened
    bool mIsOpened { };

private:

    // FatFS File object
    FIL mFile { };

};

} /* namespace FS */

#endif /* __FILE_HPP */
