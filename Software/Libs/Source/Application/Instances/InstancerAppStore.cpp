/**
 *******************************************************************************
 * @file    InstanceUserAppLoader.cpp
 * @brief   Initialization of the UserAppLoader
 * 
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "Instances/InstancerAppStore.hpp"
#include "CRC/Crc32.hpp"
#include "ComponentFactory.hpp"
#include "Vibro.hpp"
#include "Buzzer.hpp"
#include "System.hpp"
#include "HWButtons.hpp"
#include "UserAppLayer/UserAppValidator.hpp"
#include "UserAppLayer/UserAppManager.hpp"

#define LOG_MODULE_PRX      "InstAppStore"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"
#include <new>   // std::nothrow

////////////////////////////////////////////////////////////////////////////////
////
////////////////////////////////////////////////////////////////////////////////

Instance::AppStore::AppStore()
    : mStore()
    , mAppManager(App::Manager::InitInstance(&mStore,
                                             ComponentFactory::GetInstance().getBacklight(),
                                             Vibro::GetInstance(),
                                             Buzzer::GetInstance()))
{
    scanFileSystem(mStore);
}

App::Manager& Instance::AppStore::getAppManager()
{
    return mAppManager;
}

void Instance::AppStore::scanFileSystem(Store& store)
{
    constexpr char skAppsPath[] = APPS_DIR_PATH;

    // Minimum file size
    static const uint32_t skIconSize = App::kIconSmallWH * App::kIconSmallWH;

    // Max length of file name
    static const uint32_t skRelativePathLenMax = FS::FileSystem::skMaxPathLen - strlen(skAppsPath);

    constexpr char skAppExtension[] = APP_FILE_EXTENSION;

    const uint32_t kDataBuffSize = 1024;
    struct Worker {
        FS::Directory appsDir;
        Interface::IFileSystem::ObjectInfo item;
        FS::Directory appDir;
        FS::File appFile;
        char path[FS::FileSystem::skMaxPathLen + 1];
        uint8_t buffer[kDataBuffSize];
        UappHeader header;
    };

    Worker *w = new (std::nothrow) Worker;
    if (w == nullptr) {
        LOG_ERROR("No memory\n");
        return;
    }

    w->appsDir.setPath(skAppsPath);

    if (!w->appsDir.exist()) {
        w->appsDir.create();
        delete w;

        // There is no point in scanning a directory
        // if it has just been created
        return;
    }

    if (!w->appsDir.open()) {
        LOG_ERROR("Failed to open dir [%s]\n", skAppsPath);
        delete w;
        return;
    }

    while (w->appsDir.readNext(w->item)) {

            if (!w->item.isDir) {
                // Skip files
                continue;
            }

            if (strlen(skAppsPath) + strlen(w->item.name) >= sizeof(w->path)) {
                // Skip very long names
                LOG_WARNING("Long name [%s]\n", w->item.name);
                continue;
            }
            snprintf(w->path, FS::FileSystem::skMaxPathLen,
                    "%s%s/", skAppsPath, w->item.name);

            w->appDir.setPath(w->path);
            if (!w->appDir.open()) {
                LOG_WARNING("Failed to open dir [%s]\n", w->path);
                continue;
            }

            while (w->appDir.readNext(w->item)) {

                // TODO:
                if (w->item.isDir
                        || w->item.size <= skIconSize) // Pre validation
                {
                    continue;
                }

                // TODO:: Is it possible with FatFS?
                uint32_t pathLen = strlen(w->appDir.getPath()) + strlen(w->item.name);
                if ((pathLen >= sizeof(w->path)
                        || (pathLen - strlen(skAppsPath) >= skRelativePathLenMax)))
                {
                    // Skip very long names
                    LOG_WARNING("Long name [%s]\n", w->item.name);
                    continue;
                }

                snprintf(w->path, FS::FileSystem::skMaxPathLen,
                        "%s%s", w->appDir.getPath(), w->item.name);

                // Filter *.uapp files only
                int32_t nameLen = strlen(w->item.name);
                if (strcmp(&w->item.name[nameLen - strlen(skAppExtension)], skAppExtension) != 0) {
                    continue;
                }

                w->appFile.setPath(w->path);
                if (!w->appFile.open()) {
                    LOG_ERROR("Failed to open file [%s]\n", w->path);
                    continue;
                }


                //TODO: make validation
#if 0   //
                size_t br = 0;

                // Validate file CRC
                uint32_t crc = 0;
                uint32_t storedCRC = !crc;
                size_t readLeft = w->item.size - 4;
                while (readLeft > 0) {
                    size_t toRead = readLeft > sizeof(w->buffer)
                            ? sizeof(w->buffer) : readLeft;

                    if (w->appFile.read(reinterpret_cast<char*>(w->buffer), toRead, br)
                            && br == toRead)
                    {
                        readLeft -= toRead;
                        crc = CalcCrc32(w->buffer, toRead, crc);

                        if (readLeft == 0) {
                            w->appFile.read(reinterpret_cast<char*>(&storedCRC), 4, br);
                        }
                    } else {
                        break;
                    }
                }

                if (storedCRC != crc) {
                    LOG_ERROR("Failed to check CRC (crc 0x%08X != 0x%08X)\n", crc, storedCRC);
                    w->appFile.close();
                    continue;
                }

                // Return file pointer to start
                if (!w->appFile.seek(0)) {
                    w->appFile.close();
                    LOG_ERROR("Failed set file pointer\n");
                    continue;
                }

                // Read header
                if (!w->appFile.read(reinterpret_cast<char*>(&w->header), sizeof(w->header), br)
                        || br != sizeof(w->header))
                {
                    w->appFile.close();
                    LOG_ERROR("Failed read file\n");
                    continue;
                }


                // Save app information
                strncpy(mAppInfo[num].location,
                        &w->path[strlen(skAppsPath)], skRelativePathLenMax);

                strncpy(mAppInfo[num].name,
                        reinterpret_cast<const char*>(w->header.app_name), skNameLenMax);

                // Save icon
                if (w->header.icon_sz * 4 != skIconSize) {
                    w->appFile.close();
                    LOG_ERROR("Wrong icon size (icon_sz = %d bytes)\n", w->header.icon_sz * 4);
                    continue;
                }

                // Set pointer to the icon location
                if (!w->appFile.seek(sizeof(UappHeader))) {
                    w->appFile.close();
                    LOG_ERROR("Failed set file pointer\n");
                    continue;
                }

                if (!w->appFile.read(reinterpret_cast<char*>(mAppInfo[num].icon), skIconSize, br)
                    || br != skIconSize)
                {
                    w->appFile.close();
                    LOG_ERROR("Failed read file\n");
                    continue;
                }

                mAppInfo[num].pid =
                        CalcCrc32(reinterpret_cast<const uint8_t*>(mAppInfo[num].location),
                        strlen(mAppInfo[num].location), 0);

                LOG_DEBUG("App found: '%s' (pid 0x%08X) [%s] \n", mAppInfo[num].name,
                        mAppInfo[num].pid, mAppInfo[num].location);

                num++;
#endif
                Interface::IFile *pFile = new(std::nothrow) FS::File(w->appFile.getPath());

                if (pFile) {
                    Components::UserAppValidator validator(Driver::System::GetLibcVersion().u32);
                    if (validator.appIsValid(*pFile)) {
                        store.addFile(pFile);
                        LOG_DEBUG("App is valid:  [%s]\n", pFile->getPath());
                    } else {
                        LOG_INFO_WP("\n");
                        LOG_INFO_WP("//////////////////////////////////////////////////////////////////////\n");
                        LOG_INFO_WP("//// App is invalid:  [%s]\n", pFile->getPath());
                        LOG_INFO_WP("//////////////////////////////////////////////////////////////////////\n");
                        LOG_INFO_WP("\n");
                        delete pFile;
                    }
                }

                w->appFile.close();
                break;  // We are no longer looking for files in this directory

            } // while app dir
            w->appDir.close();

        } // while apps dir
        w->appsDir.close();

}

Instance::AppStore& Instance::AppStore::getInstance()
{
    static Instance::AppStore mInstance;

    return mInstance;
}

////////////////////////////////////////////////////////////////////////////////
////
////////////////////////////////////////////////////////////////////////////////

Instance::AppStore::Store::Store() : mFiles()
{}

void Instance::AppStore::Store::addFile(Interface::IFile* f)
{
    mFiles.push_back(f);
}

void Instance::AppStore::Store::scan(std::vector<App::Info>& list)
{
    for (auto f : mFiles) {
        App::Info info{};

        scan(*f, info);

        list.push_back(info);
    }
}

std::shared_ptr<uint8_t> Instance::AppStore::Store::loadIcon(const std::string& location, bool normal)
{
    for (auto f : mFiles) {
        if (f->getPath() != location) {
            continue;
        }

        if (!f->open()) {
            LOG_ERROR("failed to open the file, [%s]\n", f->getPath());
            return nullptr;
        }

        std::shared_ptr<uint8_t> result = [&]() -> std::shared_ptr<uint8_t> {
            MainHeader mainHeader{};
            size_t br;
            if (!f->read(reinterpret_cast<char*>(&mainHeader), sizeof(MainHeader), br)) {
                LOG_ERROR("failed to read the file, [%s]\n", f->getPath());
                return nullptr;
            }

            uint32_t offset = sizeof(MainHeader) + (normal ? 0 : mainHeader.normal_icon_size);
            uint32_t size = normal ? mainHeader.normal_icon_size : mainHeader.small_icon_size;

            if (!f->seek(offset)) {
                LOG_ERROR("failed to seek in the file, [%s]\n", f->getPath());
                return nullptr;
            }

            auto iconData = std::shared_ptr<uint8_t>(new uint8_t[size], std::default_delete<uint8_t[]>());

            if (!f->read(reinterpret_cast<char*>(iconData.get()), size, br) || br != size) {
                LOG_ERROR("failed to read icon data from file, [%s]\n", f->getPath());
                return nullptr;
            }

            return iconData;
        }();

        f->close();

        return result;
    }

    return nullptr;
}

void Instance::AppStore::Store::scan(Interface::IFile& f, App::Info& info)
{
    if (!f.open()) {
        LOG_ERROR("failed to open the file, [%s]\n", f.getPath());
        return;
    }

    MainHeader mainHeader{};
    size_t br;
    if (!f.read(reinterpret_cast<char*>(&mainHeader), sizeof(MainHeader), br)) {
        f.close();
        LOG_ERROR("failed to read the file, [%s]\n", f.getPath());
        return;
    }

    std::string name(f.getPath());
    std::string nameService(name + "Service");
    std::string nameGUI(name + "GUI");

    info.pidService  = CRC32::calculate(reinterpret_cast<const uint8_t*>(nameService.c_str()), nameService.length());
    info.pidGUI      = CRC32::calculate(reinterpret_cast<const uint8_t*>(nameGUI.c_str()), nameGUI.length());
    info.flags       = mainHeader.flags;
    info.name        = mainHeader.app_name;
    info.location    = f.getPath();
    info.serviceSize = mainHeader.serviceSize;
    info.guiSize     = f.size() - sizeof(MainHeader) - mainHeader.normal_icon_size - mainHeader.small_icon_size - mainHeader.serviceSize;
    info.file        = &f;

    f.close();
}
