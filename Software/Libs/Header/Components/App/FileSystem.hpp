#pragma once

#include "SDK/Interfaces/IFileSystem.hpp"
#include "IFileSystem.hpp"
#include <memory>
#include <cstring>

namespace App
{

// Forward declarations
class File;
class Directory;

/**
 * @brief Adapter class for File operations (SDK::Interface::IFile -> Interface::IFile)
 */
class File : public SDK::Interface::IFile {
public:
    File(std::unique_ptr<::Interface::IFile> impl) : mImpl(std::move(impl)) {}

    virtual ~File() = default;

    // FsObject interface
    void setPath(const char* path) override {
        mImpl->setPath(path);
    }

    const char* getPath() const override {
        return mImpl->getPath();
    }

    bool exist() const override {
        return mImpl->exist();
    }

    bool rename(const char* newPath) override {
        return mImpl->rename(newPath);
    }

    bool remove() override {
        return mImpl->remove();
    }

    // File interface
    size_t size() const override {
        return mImpl->size();
    }

    bool open(bool wMode = false, bool override = false) override {
        return mImpl->open(wMode, override);
    }

    bool isOpen() const override {
        return mImpl->isOpen();
    }

    bool close() override {
        return mImpl->close();
    }

    bool read(char* buff, size_t btr, size_t& br) override {
        return mImpl->read(buff, btr, br);
    }

    bool write(const char* buff, size_t btw, size_t& bw) override {
        return mImpl->write(buff, btw, bw);
    }

    bool seek(size_t offset) override {
        return mImpl->seek(offset);
    }

    bool truncate(size_t offset) override {
        return mImpl->truncate(offset);
    }

    bool flush() override {
        return mImpl->flush();
    }

    size_t getPosition() const override {
        return mImpl->getPosition();
    }

private:
    std::unique_ptr<::Interface::IFile> mImpl;
};

/**
 * @brief Adapter class for Directory operations (SDK::Interface::IDirectory -> Interface::IDirectory)
 */
class Directory : public SDK::Interface::IDirectory {
public:
    Directory(std::unique_ptr<::Interface::IDirectory> impl) : mImpl(std::move(impl)) {}

    virtual ~Directory() = default;

    // FsObject interface
    void setPath(const char* path) override {
        mImpl->setPath(path);
    }

    const char* getPath() const override {
        return mImpl->getPath();
    }

    bool exist() const override {
        return mImpl->exist();
    }

    bool rename(const char* newPath) override {
        return mImpl->rename(newPath);
    }

    bool remove() override {
        return mImpl->remove();
    }

    // Directory interface
    bool create() override {
        return mImpl->create();
    }

    bool open() override {
        return mImpl->open();
    }

    bool isOpen() const override {
        return mImpl->isOpen();
    }

    bool readNext(SDK::Interface::IFileSystem::ObjectInfo& item, bool reset = false) override {
        // Convert between ObjectInfo structures
        ::Interface::IFileSystem::ObjectInfo implItem;
        bool result = mImpl->readNext(implItem, reset);

        if (result) {
            // Copy data from Interface::IFileSystem::ObjectInfo to SDK::Interface::IFileSystem::ObjectInfo
            std::strncpy(item.name, implItem.name, SDK::Interface::IFileSystem::skMaxPathLen - 1);
            item.name[SDK::Interface::IFileSystem::skMaxPathLen - 1] = '\0';
            item.isDir = implItem.isDir;
            item.isHidden = implItem.isHidden;
            item.isSystem = implItem.isSystem;
            item.size = implItem.size;
            item.utc = implItem.utc;
        }

        return result;
    }

    bool close() override {
        return mImpl->close();
    }

private:
    std::unique_ptr<::Interface::IDirectory> mImpl;
};

/**
 * @brief Main FileSystem adapter class (SDK::Interface::IFileSystem -> Interface::IFileSystem)
 */
class FileSystem : public SDK::Interface::IFileSystem {
public:
    FileSystem(::Interface::IFileSystem& real) : mReal(&real) {}

    virtual ~FileSystem() = default;

    bool mkdir(const char* path) override {
        return mReal->mkdir(path);
    }

    std::unique_ptr<SDK::Interface::IFile> file(const char* path) override {
        auto implFile = mReal->file(path);
        if (implFile) {
            return std::make_unique<App::File>(std::move(implFile));
        }
        return nullptr;
    }

    std::unique_ptr<SDK::Interface::IDirectory> dir(const char* path) override {
        auto implDir = mReal->dir(path);
        if (implDir) {
            return std::make_unique<App::Directory>(std::move(implDir));
        }
        return nullptr;
    }

    bool exist(const char* path) const override {
        return mReal->exist(path);
    }

    bool remove(const char* path) override {
        return mReal->remove(path);
    }

    bool rename(const char* oldPath, const char* newPath) override {
        return mReal->rename(oldPath, newPath);
    }

    bool copy(const char* oldPath, const char* newPath) override {
        return mReal->copy(oldPath, newPath);
    }

    bool objectInfo(const char* path, SDK::Interface::IFileSystem::ObjectInfo& item) const override {
        // Convert between ObjectInfo structures
        ::Interface::IFileSystem::ObjectInfo implItem;
        bool result = mReal->objectInfo(path, implItem);

        if (result) {
            // Copy data from Interface::IFileSystem::ObjectInfo to SDK::Interface::IFileSystem::ObjectInfo
            std::strncpy(item.name, implItem.name, SDK::Interface::IFileSystem::skMaxPathLen - 1);
            item.name[SDK::Interface::IFileSystem::skMaxPathLen - 1] = '\0';
            item.isDir = implItem.isDir;
            item.isHidden = implItem.isHidden;
            item.isSystem = implItem.isSystem;
            item.size = implItem.size;
            item.utc = implItem.utc;
        }

        return result;
    }

private:
    ::Interface::IFileSystem* mReal;
};


} // namespace App
