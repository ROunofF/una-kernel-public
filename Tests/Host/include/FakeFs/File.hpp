// Host-test fake of FS::File (Components/FileSystem/File.hpp): an in-memory
// volume with failures to inject, for code that reads and writes small files
// such as Settings::ManagerBase. Only the calls that code makes are here.
//
// Not on the shared include path: a test that wants it adds this folder to
// its own source file (see CMakeLists.txt), so nothing else picks it up in
// place of the real header.

#ifndef HOST_TEST_FAKEFS_FILE_HPP
#define HOST_TEST_FAKEFS_FILE_HPP

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <map>
#include <string>

#ifndef FF_MAX_LFN
#define FF_MAX_LFN 255
#endif

namespace FS
{

/// The volume, and the failures to inject. Reset() between tests.
struct FakeVolume
{
    static inline std::map<std::string, std::string> files;

    /// Opening this path for writing fails.
    static inline std::string failWriteOpen;

    /// Renaming onto this path fails.
    static inline std::string failRenameTo;

    static void Reset()
    {
        files.clear();
        failWriteOpen.clear();
        failRenameTo.clear();
    }
};

class File
{

public:

    File() = default;

    File(const File&)            = delete;
    File& operator=(const File&) = delete;

    void setPath(const char *path) { mPath = path; }

    bool exist() const { return Exist(mPath.c_str()); }

    size_t size() const
    {
        auto it = FakeVolume::files.find(mPath);
        return it == FakeVolume::files.end() ? 0 : it->second.size();
    }

    bool open(bool wMode = false, bool override = false)
    {
        if (wMode) {
            if (mPath == FakeVolume::failWriteOpen) {
                return false;
            }
            if (override || !exist()) {
                FakeVolume::files[mPath].clear();
            }
        } else if (!exist()) {
            return false;
        }
        mOpen = true;
        mPos  = 0;
        return true;
    }

    bool close()
    {
        mOpen = false;
        return true;
    }

    bool read(char *buff, size_t btr, size_t &br)
    {
        if (!mOpen) {
            return false;
        }
        const std::string &data = FakeVolume::files[mPath];
        br = (mPos < data.size()) ? std::min(btr, data.size() - mPos) : 0;
        std::memcpy(buff, data.data() + mPos, br);
        mPos += br;
        return true;
    }

    bool write(const char *buff, size_t btw, size_t &bw)
    {
        if (!mOpen) {
            return false;
        }
        FakeVolume::files[mPath].append(buff, btw);
        bw = btw;
        return true;
    }

    bool flush() { return mOpen; }

    static bool Exist(const char *path)
    {
        return FakeVolume::files.count(path) != 0;
    }

    /// As FatFs f_rename: fails when the target exists.
    static bool Rename(const char *oldPath, const char *newPath)
    {
        if (FakeVolume::failRenameTo == newPath || !Exist(oldPath) || Exist(newPath)) {
            return false;
        }
        FakeVolume::files[newPath] = FakeVolume::files[oldPath];
        FakeVolume::files.erase(oldPath);
        return true;
    }

    static bool Remove(const char *path)
    {
        return FakeVolume::files.erase(path) != 0;
    }

private:

    std::string mPath;
    bool        mOpen = false;
    size_t      mPos  = 0;

};

} // namespace FS

#endif // HOST_TEST_FAKEFS_FILE_HPP
