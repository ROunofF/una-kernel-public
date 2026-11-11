/**
 ******************************************************************************
 * @file    GuardPath.hpp
 * @brief   Pure, dependency-free path-resolution policy for FileSystemGuard.
 *
 * Extracted from FileSystemGuard so the sandbox path rules can be host
 * unit-tested WITHOUT the RTOS/FatFs backend the guard wraps. This matters:
 * the in-memory fake filesystem used by the calibrator host tests is a
 * different IFileSystem implementation and does NOT model these rules, so a
 * guard path bug (e.g. the "../SharedData" directory being unresolvable) would
 * otherwise be invisible to the test suite.
 ******************************************************************************
 */

#ifndef __GUARD_PATH_HPP
#define __GUARD_PATH_HPP

#include <cstddef>
#include <cstdio>
#include <cstring>

namespace FsGuard
{

/// Working-buffer bound; mirrors Interface::IFileSystem::skMaxPathLen (256).
inline constexpr size_t kMaxPathLen = 256;

/// What a resolved path may be used for.
enum class Access : unsigned char {
    App,             ///< Inside the guard root.
    Shared,          ///< ../SharedData: outside the root, readable and writable.
    SharedReadOnly,  ///< A volume root (":/<name>"): readable, never modified.
};

/// True if a path of this access may be created, written, truncated,
/// renamed or removed.
inline constexpr bool canModify(Access a) { return a != Access::SharedReadOnly; }

/// Prefix naming a folder at the root of the guard's volume. FatFs rejects ':'
/// inside a name (FF_USE_LFN), so no path starting with it could open before
/// this prefix was given a meaning.
inline constexpr char kVolumeRootPrefix[] = ":/";

/// Folders an app guard may reach with kVolumeRootPrefix, all read-only:
/// ":/Workouts" is "<volume>:/Workouts".
inline constexpr const char *kVolumeRoots[] = {"Workouts"};

/**
 * @brief True only when @p path contains ".." as a whole path segment: exactly
 *        "..", a leading "../", an embedded "/../", or a trailing "/..". Names
 *        that merely contain ".." as a substring (e.g. "log..txt",
 *        "sub/..hidden") are NOT traversal and are allowed.
 */
inline bool hasParentSegment(const char *path)
{
    const size_t len = std::strlen(path);
    return (std::strcmp(path, "..") == 0) ||
           (std::strncmp(path, "../", 3) == 0) ||
           (std::strstr(path, "/../") != nullptr) ||
           (len >= 3 && std::strcmp(path + len - 3, "/..") == 0);
}

/**
 * @brief The volume part of @p rootPath, e.g. "2:" from "2:/Apps/Running":
 *        its length up to and including the first ':', or 0 if it has none.
 */
inline size_t volumeLen(const char *rootPath)
{
    const char *colon = std::strchr(rootPath, ':');
    return colon != nullptr ? static_cast<size_t>(colon - rootPath) + 1 : 0;
}

/**
 * @brief Resolve a guard-relative path to an absolute path under @p rootPath.
 *
 * Out-of-root targets:
 *   - The sibling "SharedData" directory: the directory itself
 *     ("../SharedData" / "../SharedData/") and files within it
 *     ("../SharedData/<name>"). Callers must mkdir the directory before
 *     opening a file in it, since FatFs f_open does not create parents.
 *   - With @p volumeRoots, the folders in kVolumeRoots at the root of the
 *     guard's volume: ":/Workouts" and ":/Workouts/<name>", read-only. Any
 *     other ":/" path is rejected. Without @p volumeRoots a ":/" path is an
 *     ordinary relative path, which FatFs then rejects for its ':'.
 * All other parent-directory traversal is rejected.
 *
 * @param rootPath     Guard root, with any trailing '/' already stripped.
 * @param relativePath Guard-relative input path (assumed non-null).
 * @param volumeRoots  Whether ":/" paths name volume-root folders.
 * @param buffer       Output buffer for the absolute path.
 * @param buffSize     Size of @p buffer.
 * @param access       Set to what the resolved path may be used for.
 * @retval true on success; false on rejection or truncation.
 */
inline bool resolve(const char *rootPath, const char *relativePath, bool volumeRoots,
                    char *buffer, size_t buffSize, Access &access)
{
    // Guard the boundary: callers assert non-null, but with asserts disabled a
    // null would otherwise be dereferenced here (mirrors buildRelativePath).
    if (rootPath == nullptr || relativePath == nullptr ||
        buffer == nullptr || buffSize == 0) {
        return false;
    }

    access = Access::App;

    if (volumeRoots && std::strncmp(relativePath, kVolumeRootPrefix,
                                    sizeof(kVolumeRootPrefix) - 1) == 0) {
        const char  *name = relativePath + sizeof(kVolumeRootPrefix) - 1;
        const size_t vol  = volumeLen(rootPath);
        if (vol == 0) {
            return false;
        }
        for (const char *root : kVolumeRoots) {
            const size_t n = std::strlen(root);
            if (std::strncmp(name, root, n) != 0 || (name[n] != '\0' && name[n] != '/')) {
                continue;
            }
            const char *rest = name[n] == '/' ? name + n + 1 : name + n;
            if (hasParentSegment(rest)) {
                return false;
            }
            const int written = *rest != '\0'
                ? std::snprintf(buffer, buffSize, "%.*s/%s/%s", static_cast<int>(vol),
                                rootPath, root, rest)
                : std::snprintf(buffer, buffSize, "%.*s/%s", static_cast<int>(vol),
                                rootPath, root);
            access = Access::SharedReadOnly;
            return written > 0 && written < static_cast<int>(buffSize);
        }
        return false;  // a volume root the guard does not offer
    }

    static constexpr char kSharedDataDir[]    = "../SharedData";   // the dir
    static constexpr char kSharedDataPrefix[] = "../SharedData/";  // a child
    const bool isSharedDir =
        (std::strcmp(relativePath, kSharedDataDir) == 0) ||
        (std::strcmp(relativePath, kSharedDataPrefix) == 0);
    const bool isSharedChild =
        (std::strncmp(relativePath, kSharedDataPrefix,
                      sizeof(kSharedDataPrefix) - 1) == 0) &&
        (relativePath[sizeof(kSharedDataPrefix) - 1] != '\0');
    if (isSharedDir || isSharedChild) {
        const char *lastSlash = std::strrchr(rootPath, '/');
        if (lastSlash == nullptr) {
            return false;
        }
        const int rootHead = static_cast<int>(lastSlash - rootPath);
        int written;
        if (isSharedDir) {
            // The SharedData directory itself (e.g. for mkdir) — no child name.
            written = std::snprintf(buffer, buffSize, "%.*s/SharedData",
                    rootHead, rootPath);
        } else {
            const char *filename = relativePath + sizeof(kSharedDataPrefix) - 1;
            // Reject further parent traversal in the remainder but allow names
            // that merely contain ".." (e.g. "log..txt", "sub/..hidden").
            if (hasParentSegment(filename)) {
                return false;
            }
            written = std::snprintf(buffer, buffSize, "%.*s/SharedData/%s",
                    rootHead, rootPath, filename);
        }
        access = Access::Shared;
        return written > 0 && written < static_cast<int>(buffSize);
    }

    // No other parent-directory traversal is allowed (the SharedData whitelist
    // above is the sole exception). Reject a ".." segment anywhere — "/../x" or
    // "a/../../x" would otherwise escape the root after FS normalization.
    if (hasParentSegment(relativePath)) {
        return false;
    }

    size_t pathLen = std::strlen(relativePath);
    const bool hasSlash = (*relativePath == '/');
    pathLen = hasSlash ? pathLen : pathLen + 1;
    const size_t rootPathLen = std::strlen(rootPath);
    const int total = static_cast<int>(pathLen + rootPathLen);

    if (total >= static_cast<int>(buffSize)) {
        return false;  // would not fit
    }
    if (hasSlash) {
        return std::snprintf(buffer, buffSize, "%s%s", rootPath, relativePath) == total;
    }
    return std::snprintf(buffer, buffSize, "%s/%s", rootPath, relativePath) == total;
}

/// resolve() without volume roots, ignoring the access class.
inline bool buildFullPath(const char *rootPath, const char *relativePath,
                          char *buffer, size_t buffSize)
{
    Access access;
    return resolve(rootPath, relativePath, false, buffer, buffSize, access);
}

/**
 * @brief Inverse of resolve(): map an absolute path back to its guard-relative
 *        form (root prefix stripped, the "../SharedData/<file>" form for
 *        shared-data paths, or with @p volumeRoots the ":/<root>/<file>" form).
 *        Used by FileGuard/DirectoryGuard::getPath.
 *
 * @param rootPath    Guard root, trailing '/' already stripped.
 * @param fullPath    Absolute path to map.
 * @param out         Caller-owned output buffer.
 * @param outSize     Size of @p out.
 * @param volumeRoots Whether the guard offers kVolumeRoots.
 * @retval true on success; false if inputs are invalid, the path is outside the
 *         guarded root, or the result is truncated.
 */
inline bool buildRelativePath(const char *rootPath, const char *fullPath,
                              char *out, size_t outSize, bool volumeRoots = false)
{
    if (rootPath == nullptr || fullPath == nullptr || out == nullptr || outSize == 0) {
        return false;
    }

    // Volume-root folders, e.g. "2:/Workouts/a.fit" back to ":/Workouts/a.fit".
    const size_t vol = volumeLen(rootPath);
    if (volumeRoots && vol > 0 && std::strncmp(fullPath, rootPath, vol) == 0 &&
        fullPath[vol] == '/') {
        const char *name = fullPath + vol + 1;
        for (const char *root : kVolumeRoots) {
            const size_t n = std::strlen(root);
            if (std::strncmp(name, root, n) == 0 && (name[n] == '\0' || name[n] == '/')) {
                const int w = std::snprintf(out, outSize, "%s%s", kVolumeRootPrefix, name);
                return w > 0 && w < static_cast<int>(outSize);
            }
        }
    }

    // SharedData paths resolve OUTSIDE rootPath, so the plain prefix-strip below
    // would not round-trip; map "<parent>/SharedData" back to "../SharedData"
    // and "<parent>/SharedData/<file>" back to "../SharedData/<file>". Both the
    // bare directory and child files must invert, mirroring buildFullPath().
    const char *lastSlash = std::strrchr(rootPath, '/');
    if (lastSlash != nullptr) {
        char sdDir[kMaxPathLen];
        const int d = std::snprintf(sdDir, sizeof(sdDir), "%.*s/SharedData",
                static_cast<int>(lastSlash - rootPath), rootPath);
        if (d > 0 && d < static_cast<int>(sizeof(sdDir))) {
            const size_t dlen = static_cast<size_t>(d);
            if (std::strcmp(fullPath, sdDir) == 0) {
                int w = std::snprintf(out, outSize, "../SharedData");  // the dir
                return w > 0 && w < static_cast<int>(outSize);
            }
            if (std::strncmp(fullPath, sdDir, dlen) == 0 && fullPath[dlen] == '/') {
                int w = std::snprintf(out, outSize, "../SharedData/%s",
                        fullPath + dlen + 1);  // a file within it
                return w > 0 && w < static_cast<int>(outSize);
            }
        }
    }

    // Strip the root only once fullPath is confirmed to begin with it AND the
    // match ends on a path boundary (so root "/app" does not match "/apple/x").
    const size_t rootLen = std::strlen(rootPath);
    if (rootLen >= std::strlen(fullPath) ||
        std::strncmp(fullPath, rootPath, rootLen) != 0 ||
        fullPath[rootLen] != '/') {
        return false;
    }
    int w = std::snprintf(out, outSize, "%s", fullPath + rootLen);
    return w > 0 && w < static_cast<int>(outSize);
}

} // namespace FsGuard

#endif /* __GUARD_PATH_HPP */
