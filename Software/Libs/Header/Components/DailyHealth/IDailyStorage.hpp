/**
 ******************************************************************************
 * @file    IDailyStorage.hpp
 * @brief   Persistence contract for the Daily Health subsystem.
 *
 *          The implementation is responsible for whatever I/O is needed
 *          (FS, JSON, CRC, ...). The Service treats this as an opaque
 *          black box and never spawns I/O on its own.
 *
 *          The Service itself is responsible for marshalling calls onto
 *          a single task - implementations therefore do NOT need to be
 *          callable from arbitrary stacks.
 ******************************************************************************
 */

#ifndef COMPONENT_DAILY_HEALTH_IDAILY_STORAGE_HPP
#define COMPONENT_DAILY_HEALTH_IDAILY_STORAGE_HPP

#include <ctime>

#include "DailyHealth/Types.hpp"

namespace DailyHealth
{

class IDailyStorage
{
public:
    virtual ~IDailyStorage() = default;

    /**
     * @brief Load the scalar summary for a past date.
     *
     * Parses only the small scalar fields and skips the per-minute HR
     * blob, so it stays cheap when consumers only need steps / floors /
     * RHR / AHR.
     *
     * @param date  Calendar date (year/month/day fields used).
     * @param out   Filled on success.
     * @return true on success; false if the record is missing or
     *         corrupt. Callers should treat false as "no data".
     */
    virtual bool loadRecord(std::tm date, DailyRecord& out) = 0;

    /**
     * @brief Load the per-minute HR grid for a past date.
     *
     * Returns the 24x60 matrix from the same archive file that
     * @ref loadRecord reads. On miss / corruption the matrix is zeroed
     * and false is returned.
     */
    virtual bool loadHrSeries(std::tm date, DailyHrMatrix& out) = 0;

    /**
     * @brief Persist the finalised day (called on day rollover).
     *
     * Writes both the scalar summary and the HR grid into the same
     * archive file as a single atomic save - consumers that only need
     * one of the two never see a partial file.
     */
    virtual bool saveRecord(std::tm date, const DailyRecord& summary,
                            const DailyHrMatrix& hr) = 0;

    /**
     * @brief Persist the in-progress per-day metrics for crash recovery.
     *
     * The Service calls this on its own cadence (periodic flush every
     * N minutes plus critical events). Implementations are encouraged
     * to compress these writes (only when the data changed, etc.).
     */
    virtual bool saveCurrent(std::tm date, const DailyMetrics& m) = 0;

    /**
     * @brief Restore the most recently persisted in-progress metrics.
     *
     * Called once at boot. If no recovery file exists, returns false
     * and the caller starts a fresh @c DailyMetrics for "today".
     */
    virtual bool loadCurrent(DailyMetrics& outMetrics, std::tm& outDate) = 0;

    /**
     * @brief Drop the in-progress recovery file only.
     *
     * Used after the orchestrator finalises a stale "current" snapshot
     * into a daily JSON record (e.g. on boot when the recovery file
     * belongs to yesterday).
     */
    virtual bool eraseCurrent() = 0;

    /**
     * @brief Drop all persisted state (factory reset / setDefault flow).
     */
    virtual bool eraseAll() = 0;

    /**
     * @brief Switch storage between read-write and read-only.
     *
     * When set to read-only, all save/erase operations return false;
     * loads continue to work normally. The orchestrator uses this to
     * fence storage off during USB Mass-Storage exposure (or any other
     * scenario where an external party owns the file system) - the
     * storage layer itself stays neutral about the why.
     */
    virtual void setReadOnly(bool readOnly) = 0;
};

} // namespace DailyHealth

#endif // COMPONENT_DAILY_HEALTH_IDAILY_STORAGE_HPP
