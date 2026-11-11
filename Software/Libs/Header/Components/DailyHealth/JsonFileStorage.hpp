/**
 ******************************************************************************
 * @file    JsonFileStorage.hpp
 * @brief   IDailyStorage implementation backed by FS + JSON for archived
 *          days and a CRC-protected binary blob for the in-progress day.
 *
 *          Layout:
 *           - Daily archive: {mDirPath}{YYYY}{MM}/dh_{YYYY}{MM}{DD}.json
 *             A flat JSON map with 6 fields. The schema matches the
 *             contract with the BLE companion app and must not be
 *             changed without coordination.
 *           - In-progress recovery: {mTmpFilePath}, single file. Layout:
 *               [DailyDate date][DailyMetrics m][uint32_t crc32]
 *             CRC32 is computed over [date + m] only. The file is
 *             overwritten in-place; no rotation.
 *
 *          Threading: this class is single-task safe - the Service owns
 *          the marshalling onto its own task and never calls Storage
 *          from the BLE / sensor / UI stacks directly.
 *
 *          Read-only fence: setReadOnly(true) flips a flag that makes
 *          all save/erase operations return false. Reads still work.
 *          The orchestrator (Service) uses this when an external party
 *          (USB MSD, ...) owns the file system; storage itself stays
 *          neutral about the reason.
 ******************************************************************************
 */

#ifndef COMPONENT_DAILY_HEALTH_JSON_FILE_STORAGE_HPP
#define COMPONENT_DAILY_HEALTH_JSON_FILE_STORAGE_HPP

#include <cstdint>
#include <ctime>

#include "File.hpp"

#include "DailyHealth/IDailyStorage.hpp"

namespace DailyHealth
{

class JsonFileStorage : public IDailyStorage
{
public:
    /**
     * @param dirPath       Directory holding daily archive subfolders.
     *                      Must end with a path separator (e.g. "2:/dh/").
     * @param tmpFilePath   Full path of the recovery file used for the
     *                      in-progress day.
     */
    JsonFileStorage(const char* dirPath, const char* tmpFilePath);

    ~JsonFileStorage() override = default;

    bool loadRecord(std::tm date, DailyRecord& out)                override;
    bool loadHrSeries(std::tm date, DailyHrMatrix& out)            override;
    bool saveRecord(std::tm date, const DailyRecord& summary,
                    const DailyHrMatrix& hr)                       override;
    bool saveCurrent(std::tm date, const DailyMetrics& m)          override;
    bool loadCurrent(DailyMetrics& outMetrics, std::tm& outDate)   override;
    bool eraseCurrent()                                            override;
    bool eraseAll()                                                override;
    void setReadOnly(bool readOnly)                                override;

private:
    /// Working buffer used for path generation, JSON serialisation
    /// and the binary recovery payload. The three uses never overlap
    /// in time within a single call: path is built first, FS::File
    /// copies it internally on setPath(), and only after that does
    /// the buffer get rewritten with file content.
    ///
    /// Sized to fit the worst-case archive JSON (~150 B of scalars +
    /// "hrPerMinute":[...] with 1440 three-digit entries =~ 5.9 KB)
    /// with comfortable slack.
    static constexpr size_t kBufferSize = 8192;

    const char* mDirPath;
    const char* mTmpFilePath;
    bool        mReadOnly;

    FS::File mFile;
    char     mBuffer[kBufferSize];

    /// Build "{dir}{YYYY}{MM}/" into mBuffer.
    void buildArchiveDirPath(int year, int mon);

    /// Build "{dir}{YYYY}{MM}/dh_{YYYY}{MM}{DD}.json" into mBuffer.
    void buildArchiveFilePath(int year, int mon, int mday);

    bool writeRecordJson(const char* path,
                         const DailyRecord& r,
                         const DailyHrMatrix& hr);
    bool readRecordJson(const char* path, DailyRecord& out);
    bool readHrSeriesJson(const char* path, DailyHrMatrix& out);
};

} // namespace DailyHealth

#endif // COMPONENT_DAILY_HEALTH_JSON_FILE_STORAGE_HPP
