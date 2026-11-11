/******************************************************************************
 * @file    MemoryFile.hpp
 * @brief   In-memory file implementation using byte array buffer
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __MEMORY_FILE_HPP
#define __MEMORY_FILE_HPP

#include "IFileSystem.hpp"
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace FS
{

/**
 * @brief   In-memory file implementation.
 *
 * Provides file-like interface over a byte array buffer. The buffer is
 * immutable and operates in read-only mode. All write operations will fail.
 */
class MemoryFile : public Interface::IFile {
public:
    /**
     * @brief   Constructor.
     * @param   buff: Pointer to the byte array buffer.
     * @param   size: Size of the buffer in bytes.
     */
    MemoryFile(const uint8_t* buff, uint32_t size);

    /**
     * @brief   Destructor.
     */
    virtual ~MemoryFile() = default;

    /**
     * @brief   Sets the path of the object (not supported).
     * @param   path: Path to the file or directory.
     */
    virtual void setPath(const char *path) override;

    /**
     * @brief   Returns the object's path.
     * @retval  Pointer to the path string with base address information.
     */
    virtual const char* getPath() const override;

    /**
     * @brief   Checks if the object exists.
     * @retval  'true' if buffer is valid, 'false' otherwise.
     */
    virtual bool exist() const override;

    /**
     * @brief   Rename the object (not supported).
     * @param   newPath: New object path.
     * @retval  'false' always, as renaming is not supported.
     */
    virtual bool rename(const char *newPath) override;

    /**
     * @brief   Removes the object from the filesystem (not supported).
     * @retval  'false' always, as removal is not supported.
     */
    virtual bool remove() override;

    /**
     * @brief   Returns the size of the file in bytes.
     * @retval  File size in bytes.
     */
    virtual size_t size() const override;

    /**
     * @brief   Opens the file.
     * @param   wMode: Open in write mode if true, otherwise in read mode.
     * @param   override: Override the file if it exists.
     * @retval  'true' if opened in read mode, 'false' if write mode requested.
     */
    virtual bool open(bool wMode = false, bool override = false) override;

    /**
     * @brief   Checks if the file is open.
     * @retval  'true' if the file is open, 'false' otherwise.
     */
    virtual bool isOpen() const override;

    /**
     * @brief   Closes the file.
     * @retval  'true' always, as closing always succeeds.
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
     * @brief   Writes data to the file (not supported).
     * @param   buff: Buffer containing the data to write.
     * @param   btw: Number of bytes to write.
     * @param   bw: Reference to store the number of bytes actually written.
     * @retval  'false' always, as writes are not supported.
     */
    virtual bool write(const char *buff, size_t btw, size_t &bw) override;

    /**
     * @brief   Sets the current file position.
     * @param   offset: New position in bytes.
     * @retval  'true' if the position was successfully set, 'false' otherwise.
     */
    virtual bool seek(size_t offset) override;

    /**
     * @brief   Truncates the file at the given offset (not supported).
     * @param   offset: Position to truncate the file.
     * @retval  'false' always, as truncation is not supported.
     */
    virtual bool truncate(size_t offset) override;

    /**
     * @brief   Synchronizes the file's data to the storage medium.
     * @retval  'true' always, as no synchronization is needed.
     */
    virtual bool flush() override;

    /**
     * @brief   Returns the current position in the file.
     * @retval  Current file position in bytes.
     */
    virtual size_t getPosition() const override;

private:
    const uint8_t* mBuffer;
    uint32_t mSize;
    uint32_t mPosition;
    bool mIsOpen;
    mutable char mPathBuffer[64];
};

} // namespace FS

#endif // __MEMORY_FILE_HPP
