/**
 ******************************************************************************
 * @file    SettingsManagerBase.hpp
 * @brief   Base class for settings management.
 ******************************************************************************
 */

#ifndef __SETTINGS_MANAGER_BASE_HPP
#define __SETTINGS_MANAGER_BASE_HPP

#include <cstdint>
#include <cstdio>
#include "OS.hpp"
#include "File.hpp"
#include "Logger_fwd.h"

#define SETTINGS_BASE_LOG   "Settings"

namespace Settings
{

template<typename SettingsType, size_t BufferSize = 1024, uint32_t MutexTimeout = 5000>
class ManagerBase
{
public:
    ManagerBase(const char* filePath)
        : mFilePath(filePath)
    {
        mMutex.init("Settings", true);
        mSettingsFile.setPath(mFilePath);
    }

    virtual ~ManagerBase() = default;

    const char* getFilePath() const { return mFilePath; }

    bool load() {
        bool result = false;
        if (mMutex.lock(skMutexTimeout)) {
            result = loadFromFile();
            mMutex.release();
        }
        return result;
    }

    void setDefault() {
        SettingsType defaultSettings{};
        if (mMutex.lock(skMutexTimeout)) {
            mSettings = defaultSettings;
            saveToFile();
            mMutex.release();
        }
    }

    SettingsType getSettings() {
        SettingsType tmp{};
        if (mMutex.lock(skMutexTimeout)) {
            tmp = mSettings;
            mMutex.release();
        }
        return tmp;
    }

    bool saveSettings(const SettingsType& settings) {
        bool status = false;
        if (mMutex.lock(skMutexTimeout)) {
            mSettings = settings;
            status = saveToFile();
            mMutex.release();
        }
        return status;
    }

    // Refresh the ".bak" backup from the CURRENT on-disk primary, verbatim.
    //
    // The global settings file is written almost exclusively by paths that
    // bypass write() - the phone over BLE FTS and the user over USB MSD - so the
    // atomic write()/rotate never runs for them and ".bak" is otherwise never
    // refreshed (a watch only ever written externally would have no fallback at
    // all). Call this right after an external write so loadFromFile() always has
    // a valid, current backup to recover a torn primary from.
    //
    // The copy is VERBATIM - the raw primary bytes, not a create() round-trip -
    // because the on-disk JSON carries fields the firmware does not parse or
    // emit (e.g. profile dateOfBirth/gender); a re-serialize would silently drop
    // them. The primary is read + parse-validated first, so a missing or corrupt
    // primary is a no-op and never clobbers a good ".bak". Publication is atomic
    // via the ".tmp" sibling (staged + fsynced before replacing ".bak").
    bool refreshBackup() {
        bool ok = false;
        if (mMutex.lock(skMutexTimeout)) {
            makeSiblingPath(mTmpPath, sizeof(mTmpPath), ".tmp");
            makeSiblingPath(mBakPath, sizeof(mBakPath), ".bak");

            // Validate the primary without disturbing the live mSettings (the
            // caller's load() already applied it): parse into a throwaway.
            size_t len = 0;
            SettingsType probe{};
            if (read(mSettingsFile, mBuffer, sizeof(mBuffer), len)
                    && parse(mBuffer, len, probe)) {
                // Stage the raw primary bytes to ".tmp" (fsync + close), then
                // replace ".bak" (FatFs f_rename fails onto an existing target,
                // so clear the old .bak first). A crash in the tiny
                // remove->rename window leaves ".bak" absent, but the primary is
                // intact and good, so recovery is unaffected.
                mSettingsFile.setPath(mTmpPath);
                bool staged = false;
                if (mSettingsFile.open(true, true)) {
                    size_t bw = 0;
                    staged = (mSettingsFile.write(mBuffer, len, bw) && bw == len
                            && mSettingsFile.flush());
                    mSettingsFile.close();
                }
                mSettingsFile.setPath(mFilePath);

                if (staged) {
                    if (FS::File::Exist(mBakPath)) {
                        FS::File::Remove(mBakPath);
                    }
                    ok = FS::File::Rename(mTmpPath, mBakPath);
                } else if (FS::File::Exist(mTmpPath)) {
                    FS::File::Remove(mTmpPath);
                }
            }
            mMutex.release();
        }
        return ok;
    }

protected:
    // Pure virtual methods to be implemented by derived classes
    virtual bool parse(const char* buff, size_t length, SettingsType& settings) = 0;
    virtual size_t create(const SettingsType& settings, char* buff, size_t buffSize) = 0;

private:
    SettingsType mSettings{};
    const char* mFilePath;
    FS::File mSettingsFile;

    static const uint32_t skMutexTimeout = MutexTimeout;
    OS::Mutex mMutex;
    char mBuffer[BufferSize];

    // Scratch sibling-path buffers (".tmp"/".bak") for the atomic write and
    // recovery paths, kept off the stack like mBuffer. Each is ~256 B, and the
    // loadFromFile()->write() recovery nesting would otherwise put both pairs
    // (~1 KB) on the stack at once - too much for the tight 4 KB threads some
    // load() calls run on (Power onDisconnect, AppInstance accessories.json).
    // Access is serialized under mMutex, and sharing them across the
    // loadFromFile()->write() nesting is safe: loadFromFile() finishes reading
    // its paths before calling write() and does not use them afterwards.
    char mTmpPath[FF_MAX_LFN + 1];
    char mBakPath[FF_MAX_LFN + 1];

    bool read(FS::File& file, char* buffer, size_t size, size_t& len) {
        bool rv = false;
        size_t br = 0;

        if(!file.exist()) {
            return false;
        }

        len = file.size();
        if (len < size) {
            if (file.open()) {
                rv = (file.read(buffer, len, br) && br > 0);
                file.close();
            }
        }
        return rv;
    }

    // Build the sibling path "<mFilePath><suffix>" (e.g. ".tmp"/".bak").
    void makeSiblingPath(char* out, size_t outSize, const char* suffix) const {
        snprintf(out, outSize, "%s%s", mFilePath, suffix);
    }

    // Atomically publish `size` bytes of `buffer` to the primary file, so a
    // reset mid-write can never leave it truncated. The new copy is written to
    // a sibling temp (fsynced + closed) before anything touches the live file;
    // the previous good copy is then rotated to a ".bak" that loadFromFile()
    // falls back to. On any failure before the final rename the primary file is
    // left untouched. mSettingsFile is reused for the temp I/O (its path is
    // always restored to the primary) to keep the file object off the stack.
    //
    // Note: settings files live on the user volume (2:), which is exposed over
    // USB Mass Storage, so the ".tmp"/".bak" siblings are intentionally visible
    // on a PC. The ".bak" is persistent by design - it is the recovery source
    // for loadFromFile() and is only rotated afresh by the next successful save.
    bool write(const char* buffer, size_t size) {
        makeSiblingPath(mTmpPath, sizeof(mTmpPath), ".tmp");
        makeSiblingPath(mBakPath, sizeof(mBakPath), ".bak");

        // 1) Write the new copy to the temp file, fsync and close it. Never
        //    touch the live file on any failure - the old good copy survives.
        mSettingsFile.setPath(mTmpPath);
        bool ok = false;
        if (mSettingsFile.open(true, true)) {
            size_t bw = 0;
            ok = (mSettingsFile.write(buffer, size, bw) && bw == size
                    && mSettingsFile.flush());
            mSettingsFile.close();
        }
        mSettingsFile.setPath(mFilePath);

        if (!ok) {
            if (FS::File::Exist(mTmpPath)) {
                FS::File::Remove(mTmpPath);
            }
            return false;
        }

        // 2) Publish. Keep a good copy present at every crash point.
        if (FS::File::Exist(mFilePath)) {
            // Normal update: rotate the current good primary to .bak (the
            // primary stays intact until the rotate; FatFs f_rename fails onto
            // an existing target, so clear any stale .bak first), then promote
            // the temp. A crash after the rotate but before the promote leaves
            // .bak = old good, recovered by loadFromFile().
            if (FS::File::Exist(mBakPath)) {
                FS::File::Remove(mBakPath);
            }
            FS::File::Rename(mFilePath, mBakPath);   // primary absent, .bak = old good
            return FS::File::Rename(mTmpPath, mFilePath);  // primary = new good
        }

        // First write, or recovery from .bak: there is no primary to rotate.
        // Promote the temp directly and do NOT remove any existing .bak first,
        // so a valid copy (.bak, in the recovery case) stays present right up to
        // the atomic promote - closing the both-copies-absent window.
        return FS::File::Rename(mTmpPath, mFilePath);
    }

    bool saveToFile() {
        size_t len = create(mSettings, mBuffer, sizeof(mBuffer));
        if (len > 0) {
            return write(mBuffer, len);
        }
        return false;
    }

    // Recovery order is primary then ".bak" only. A ".tmp" is deliberately NOT
    // a recovery source: it may hold a half-written copy from an interrupted
    // write (it is fsynced only just before the atomic rename), so a stray one
    // is dropped rather than trusted.
    //
    // Note: on the recovery path this rewrites the primary from the backup, so
    // load() can now WRITE to disk - a deliberate change from a pure read. It is
    // safe: load() already holds mMutex, and write() serializes through it too.
    bool loadFromFile() {
        makeSiblingPath(mTmpPath, sizeof(mTmpPath), ".tmp");
        makeSiblingPath(mBakPath, sizeof(mBakPath), ".bak");

        // Primary copy: if it reads and parses, we are done. Drop any stray
        // temp left behind by a write that was interrupted before its rename.
        size_t len = 0;
        if (read(mSettingsFile, mBuffer, sizeof(mBuffer), len)
                && parse(mBuffer, len, mSettings)) {
            if (FS::File::Exist(mTmpPath)) {
                FS::File::Remove(mTmpPath);
            }
            return true;
        }

        // Primary missing/corrupt: recover from the backup written by write().
        // Reuse mSettingsFile for the read, then restore its primary path.
        mSettingsFile.setPath(mBakPath);
        len = 0;
        const bool haveBackup = (read(mSettingsFile, mBuffer, sizeof(mBuffer), len)
                && parse(mBuffer, len, mSettings));
        mSettingsFile.setPath(mFilePath);

        if (haveBackup) {
            LOGM(SETTINGS_BASE_LOG, LOG_LEVEL_WARNING,
                    "Settings '%s' recovered from backup\n", mFilePath);
            // Re-establish the primary from the recovered backup (mBuffer still
            // holds the parsed backup bytes). Remove a corrupt primary first so
            // write() takes its no-primary path - promoting the temp WITHOUT
            // rotating/removing the good .bak, so a valid copy is present right
            // up to the atomic promote. (In the missing-primary case the file is
            // already absent, so the Remove is a harmless no-op.) The .bak is
            // left in place - it still holds the good recovered contents; the
            // next successful save rotates it afresh.
            if (FS::File::Exist(mFilePath)) {
                FS::File::Remove(mFilePath);
            }
            write(mBuffer, len);
            return true;
        }

        // All copies gone or corrupt: caller keeps the RAM defaults.
        return false;
    }
};

} /* namespace Settings */

#endif /* __SETTINGS_MANAGER_BASE_HPP */
