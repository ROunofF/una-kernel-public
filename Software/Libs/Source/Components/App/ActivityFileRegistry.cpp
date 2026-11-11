/**
 ******************************************************************************
 * @file    ActivityFileRegistry.cpp
 * @brief   Persistent unique file path registry backed by a plain-text file.
 ******************************************************************************
 */

#include "App/ActivityFileRegistry.hpp"

#define LOG_MODULE_PRX      "ActivityFileRegistry"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

ActivityFileRegistry::ActivityFileRegistry(Interface::IFileSystem &fs,
                                           const char *filePath) :
        mFs(fs)
{
    snprintf(mFilePath, sizeof(mFilePath), "%s", filePath);
}

// ---------------------------------------------------------------------------
// IFileEventListener
// ---------------------------------------------------------------------------

void ActivityFileRegistry::onFileAppeared(const char *relativePath)
{
    // Filtering by extension / subdirectory goes here before calling add().
    add(relativePath);
}

void ActivityFileRegistry::onFileDisappeared(const char *relativePath)
{
    // Filtering by extension / subdirectory goes here before calling remove().
    remove(relativePath);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool ActivityFileRegistry::readLines(std::vector<char> &outBuf,
                                     std::vector<std::string_view> &outLines,
                                     Interface::IFile &file)
{
    size_t fileSize = file.size();

    outBuf.resize(fileSize + kReadReserve);

    size_t br = 0;

    if (fileSize > 0) {
        if (!file.read(outBuf.data(), fileSize, br)) {
            LOG_ERROR("Failed to read registry file\n");
            return false;
        }
    }

    // Parse lines - iterate over the bytes actually read
    char *p   = outBuf.data();
    char *end = outBuf.data() + br;

    while (p < end) {
        char *lineStart = p;

        while (p < end && *p != '\n' && *p != '\r') {
            ++p;
        }

        size_t lineLen = static_cast<size_t>(p - lineStart);

        if (lineLen > 0) {
            outLines.emplace_back(lineStart, lineLen);
        }

        // Skip \r\n, \n, \r
        while (p < end && (*p == '\n' || *p == '\r')) {
            ++p;
        }
    }

    return true;
}

bool ActivityFileRegistry::writeLines(const std::vector<std::string_view> &lines,
                                      Interface::IFile &file)
{
    for (const std::string_view &line : lines) {
        size_t bw = 0;

        if (!file.write(line.data(), line.size(), bw) || bw != line.size()) {
            LOG_ERROR("Failed to write line to registry file\n");
            return false;
        }

        if (!file.write("\n", 1, bw) || bw != 1) {
            LOG_ERROR("Failed to write newline to registry file\n");
            return false;
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Registry operations
// ---------------------------------------------------------------------------

bool ActivityFileRegistry::add(const char *path)
{
    auto f = mFs.file(mFilePath);
    if (!f) {
        LOG_ERROR("Failed to create file object\n");
        return false;
    }

    // Open for read+write without truncation. Creates the file if absent.
    if (!f->open(true, false)) {
        LOG_ERROR("Failed to open registry file\n");
        return false;
    }

    std::vector<char>             buf;
    std::vector<std::string_view> lines;

    if (!readLines(buf, lines, *f)) {
        f->close();
        return false;
    }

    // Check for duplicate
    for (const std::string_view &line : lines) {
        if (line == path) {
            LOG_DEBUG("Path already present, skipping: %s\n", path);
            f->close();
            return false;
        }
    }

    // string_view points to caller's 'path' - valid for the duration of this call
    lines.emplace_back(path, strlen(path));

    if (!f->seek(0) || !f->truncate(0)) {
        LOG_ERROR("Failed to rewind registry file\n");
        f->close();
        return false;
    }

    bool ok = writeLines(lines, *f);

    f->close();
    return ok;
}

bool ActivityFileRegistry::remove(const char *path)
{
    auto f = mFs.file(mFilePath);
    if (!f) {
        LOG_ERROR("Failed to create file object\n");
        return false;
    }

    // Open for read+write without truncation.
    if (!f->open(true, false)) {
        LOG_ERROR("Failed to open registry file\n");
        return false;
    }

    std::vector<char>             buf;
    std::vector<std::string_view> lines;

    if (!readLines(buf, lines, *f)) {
        f->close();
        return false;
    }

    // Find and erase the matching entry
    bool found = false;
    for (auto it = lines.begin(); it != lines.end(); ++it) {
        if (*it == path) {
            lines.erase(it);
            found = true;
            break;
        }
    }

    if (!found) {
        LOG_DEBUG("Path not found in registry: %s\n", path);
        f->close();
        return false;
    }

    if (!f->seek(0) || !f->truncate(0)) {
        LOG_ERROR("Failed to rewind registry file\n");
        f->close();
        return false;
    }

    bool ok = writeLines(lines, *f);

    f->close();
    return ok;
}
