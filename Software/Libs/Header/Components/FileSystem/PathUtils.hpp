/**
 ******************************************************************************
 * @file    PathUtils.hpp
 * @brief   Utility functions for file path manipulation.
 ******************************************************************************
 */

#ifndef __PATH_UTILS_HPP
#define __PATH_UTILS_HPP

#include <cstddef>

/**
 * @brief   Returns a pointer to the file extension part of a path string,
 *          including the leading dot (e.g., ".fit").
 *          Resets on directory separators so dots in directory names are ignored.
 * @param   path: File path string.
 * @return  Pointer to the '.' starting the extension, or nullptr if not found.
 */
inline const char* pathGetExtension(const char *path)
{
    if (path == nullptr) {
        return nullptr;
    }

    const char *lastDot = nullptr;
    const char *p = path;

    while (*p != '\0') {
        if (*p == '.') {
            lastDot = p;
        } else if (*p == '/' || *p == '\\') {
            lastDot = nullptr;
        }
        ++p;
    }

    return lastDot;
}

/**
 * @brief   Case-insensitive comparison of two null-terminated strings.
 * @param   a: First string.
 * @param   b: Second string.
 * @return  true if strings are equal ignoring ASCII case, false otherwise.
 */
inline bool strEqualCI(const char *a, const char *b)
{
    if (a == nullptr || b == nullptr) {
        return false;
    }

    while (*a != '\0' && *b != '\0') {
        char ca = (*a >= 'A' && *a <= 'Z') ? (*a + ('a' - 'A')) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (*b + ('a' - 'A')) : *b;
        if (ca != cb) {
            return false;
        }
        ++a;
        ++b;
    }

    return (*a == '\0' && *b == '\0');
}

#endif /* __PATH_UTILS_HPP */
