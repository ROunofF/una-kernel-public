/******************************************************************************
 * @file    MemoryFile.cpp
 * @brief   In-memory file implementation using byte array buffer
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "MemoryFile.hpp"

namespace FS
{

MemoryFile::MemoryFile(const uint8_t* buff, uint32_t size)
    : mBuffer(buff)
    , mSize(size)
    , mPosition(0)
    , mIsOpen(false)
{
    mPathBuffer[0] = '\0';
}

void MemoryFile::setPath(const char *path)
{
    // Not supported for virtual memory files
}

const char* MemoryFile::getPath() const
{
    snprintf(mPathBuffer, sizeof(mPathBuffer),
             "Virtual file. Base address 0x%08X",
             reinterpret_cast<uintptr_t>(mBuffer));
    return mPathBuffer;
}

bool MemoryFile::exist() const
{
    return (mBuffer != nullptr && mSize > 0);
}

bool MemoryFile::rename(const char *newPath)
{
    return false;
}

bool MemoryFile::remove()
{
    return false;
}

size_t MemoryFile::size() const
{
    return mSize;
}

bool MemoryFile::open(bool wMode, bool override)
{
    if (wMode) {
        return false;
    }

    mIsOpen = true;
    mPosition = 0;
    return true;
}

bool MemoryFile::isOpen() const
{
    return mIsOpen;
}

bool MemoryFile::close()
{
    mIsOpen = false;
    mPosition = 0;
    return true;
}

bool MemoryFile::read(char *buff, size_t btr, size_t &br)
{
    if (!mIsOpen || mBuffer == nullptr || buff == nullptr) {
        br = 0;
        return false;
    }

    if (mPosition >= mSize) {
        br = 0;
        return true;
    }

    size_t bytesAvailable = mSize - mPosition;
    br = (btr < bytesAvailable) ? btr : bytesAvailable;

    memcpy(buff, mBuffer + mPosition, br);
    mPosition += br;

    return true;
}

bool MemoryFile::write(const char *buff, size_t btw, size_t &bw)
{
    bw = 0;
    return false;
}

bool MemoryFile::seek(size_t offset)
{
    if (!mIsOpen) {
        return false;
    }

    if (offset > mSize) {
        return false;
    }

    mPosition = offset;
    return true;
}

bool MemoryFile::truncate(size_t offset)
{
    return false;
}

bool MemoryFile::flush()
{
    return true;
}

size_t MemoryFile::getPosition() const
{
    return mPosition;
}

} // namespace FS
