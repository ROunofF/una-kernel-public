/**
 ******************************************************************************
 * @file    AliasRules.cpp
 * @brief   Pure decision logic for variant-alias .uapp files.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "App/AliasRules.hpp"

#define LOG_MODULE_PRX      "App.Alias"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <new>

namespace App
{

bool ReadAliasPayload(Interface::IFile &file, App::VariantAliasPayload &payload)
{
    if (!file.open()) {
        LOG_ERROR("Failed to open the file, [%s]\n", file.getPath());
        return false;
    }

    bool res = file.seek(App::kAliasPayloadOffset);
    if (res) {
        // FatFs reports success with a short count at EOF, so check br too.
        size_t br = 0;
        res = file.read(reinterpret_cast<char*>(&payload),
                sizeof(App::VariantAliasPayload), br) &&
                br == sizeof(App::VariantAliasPayload);
    }

    file.close();

    if (!res) {
        LOG_ERROR("Failed to read the alias payload, [%s]\n", file.getPath());
    }

    return res;
}

AliasResolution ResolveAlias(const App::MainHeader &alias,
                             const App::VariantAliasPayload &payload,
                             const AliasTargetView *targets,
                             size_t count)
{
    AliasResolution result;

    const AliasTargetView *target = nullptr;
    for (size_t i = 0; i < count; i++) {
        if (targets[i].appID == alias.uappID) {
            result.status = AliasResolution::Status::kIdCollision;
            return result;
        }
        if (target == nullptr &&
                !targets[i].isVariant && targets[i].appID == payload.targetAppID) {
            target = &targets[i];
            result.targetIndex = i;
        }
    }

    if (target == nullptr) {
        result.status = AliasResolution::Status::kNoTarget;
        return result;
    }

    if (target->version < payload.minTargetVersion) {
        result.status = AliasResolution::Status::kTargetTooOld;
        return result;
    }

    if ((target->flags & FLAG_APP_TYPE_MASK) == FLAG_APP_TYPE_GLANCE) {
        result.status = AliasResolution::Status::kGlanceTarget;
        return result;
    }

    uint32_t flags = alias.flags;

    if ((flags & FLAG_APP_TYPE_MASK) != (target->flags & FLAG_APP_TYPE_MASK)) {
        flags = (flags & ~FLAG_APP_TYPE_MASK) | (target->flags & FLAG_APP_TYPE_MASK);
        result.typeAdjusted = true;
    }

    flags &= ~(FLAG_APP_AUTO_START | FLAG_APP_HAS_GLANCE_INTF | FLAG_APP_VARIANT_ALIAS);

    result.status = AliasResolution::Status::kOk;
    result.flags = flags;

    return result;
}

DirPick PickUappInDir(Interface::IDirectory &dir,
                      const char *dirPath,
                      const char *extension,
                      size_t minFileSize,
                      const std::function<bool(const char *path, App::MainHeader &header)> &readHeader,
                      char *pathOut,
                      size_t pathOutSize)
{
    // Off the stack: an ObjectInfo plus a scratch path are too big for the
    // scanning thread's stack (same reason Manager::scan() heap-allocates
    // its Worker).
    struct Work {
        Interface::IFileSystem::ObjectInfo item;
        char path[Interface::IFileSystem::skMaxPathLen + 1];
    };

    std::unique_ptr<Work> w(new (std::nothrow) Work);
    if (!w) {
        LOG_ERROR("No memory\n");
        return DirPick::kNone;
    }

    const size_t extLen = strlen(extension);
    DirPick picked = DirPick::kNone;

    while (dir.readNext(w->item)) {

        if (w->item.isDir || w->item.size <= minFileSize) {
            continue;
        }

        size_t nameLen = strlen(w->item.name);
        if (nameLen < extLen ||
                strcmp(&w->item.name[nameLen - extLen], extension) != 0) {
            continue;
        }

        int written = snprintf(w->path, sizeof(w->path), "%s%s",
                dirPath, w->item.name);
        if (written < 0 ||
                static_cast<size_t>(written) >= Interface::IFileSystem::skMaxPathLen) {
            LOG_WARNING("Long name [%s]\n", w->item.name);
            continue;
        }

        App::MainHeader header {};
        if (!readHeader(w->path, header) || !App::IsAliasHeader(header)) {
            // A real app (or an unreadable candidate that addApp() will
            // report) always wins and ends the walk.
            snprintf(pathOut, pathOutSize, "%s", w->path);
            return DirPick::kRealApp;
        }

        if (picked == DirPick::kNone) {
            snprintf(pathOut, pathOutSize, "%s", w->path);
            picked = DirPick::kAlias;
        }
    }

    return picked;
}

} // namespace App
