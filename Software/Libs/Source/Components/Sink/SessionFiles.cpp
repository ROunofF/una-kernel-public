/**
 ******************************************************************************
 * @file    SessionFiles.cpp
 * @brief   Session-file directory helper: next index + count-based retention.
 ******************************************************************************
 */

#include "Sink/SessionFiles.hpp"
#include "Sink/SessionFilesDetail.hpp"

#include "Directory.hpp"
#include "File.hpp"

#define LOG_MODULE_PRX      "SessionFiles"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include <cstring>
#include <string>

namespace Sink {
namespace SessionFiles {

uint32_t prepare(const char* dir, const char* prefix, const char* suffix,
                 uint32_t keepNewest)
{
    if (dir == nullptr || prefix == nullptr || suffix == nullptr) {
        return 1;
    }

    FS::Directory d(dir);
    if (!d.open()) {
        return 1;                               // directory not created yet
    }

    const size_t prefixLen = std::strlen(prefix);
    const size_t suffixLen = std::strlen(suffix);

    uint32_t            maxN  = 0;
    uint32_t            count = 0;
    detail::VictimCache cache;                  // bounded cache of the oldest seen

    Interface::IFileSystem::ObjectInfo info;
    while (d.readNext(info)) {
        if (info.isDir) {
            continue;
        }

        uint32_t idx = 0;
        if (!detail::parseIndex(info.name, std::strlen(info.name),
                                prefix, prefixLen, suffix, suffixLen, idx)) {
            continue;
        }

        ++count;
        if (idx > maxN) {
            maxN = idx;
        }
        if (keepNewest != 0) {
            cache.consider(idx, info.name);
        }
    }
    d.close();                                  // release the lock before deleting

    // Delete the oldest so the new file lands inside the budget.
    const uint32_t toDelete = detail::deleteCount(count, keepNewest, cache.tracked);
    std::string    path;
    for (uint32_t i = 0; i < toDelete; ++i) {
        path  = dir;
        path += cache.entries[i].name;
        if (FS::File::Remove(path.c_str())) {
            LOG_INFO("pruned %s\n", cache.entries[i].name.c_str());
        } else {
            LOG_WARNING("prune failed %s\n", cache.entries[i].name.c_str());
        }
    }

    return maxN + 1;
}

}  // namespace SessionFiles
}  // namespace Sink
