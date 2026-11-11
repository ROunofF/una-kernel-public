/**
 ******************************************************************************
 * @file    CSVLogger.hpp
 * @brief   CSV record formatter over a rolling session file.
 ******************************************************************************
 */

#ifndef COMPONENT_LOGGING_CSV_LOGGER_HPP
#define COMPONENT_LOGGING_CSV_LOGGER_HPP

#include <cstdint>
#include <string>

#include "Sink/SessionFileWriter.hpp"

namespace Component {

/**
 * @brief Formats comma-separated records into a rolling session file.
 *
 * Naming, count-based retention and size rollover are owned by the backing
 * SessionFileWriter; this class adds CSV formatting and re-emits the header at
 * the start of every segment so each file is self-describing. Files are named
 * <dir><base>_<id>_<NNNN>_<ts>.csv.
 */
class CSVLogger
{
public:
    /**
     * @brief Construct with the column header re-emitted at the top of every
     *        segment.
     * @param header Comma-separated column names, WITHOUT a trailing newline -
     *        writeHeader() appends the line break itself (see setHeader()).
     */
    explicit CSVLogger(const char* header = nullptr);

    /**
     * @brief Set/replace the column header.
     * @param header Comma-separated column names. Must NOT contain a trailing
     *        '\n': the logger writes the header followed by exactly one newline
     *        when it opens each segment. A trailing newline here would produce a
     *        blank line under the header on every file/segment.
     */
    void setHeader(const char* header);

    /**
     * @brief Session naming. Files become <dir><base>_<id>_<NNNN>_<ts>.csv.
     *        Call before open().
     */
    void setNaming(const char* dir, const char* base, const char* id);

    /// Keep the newest @p keepNewest session files; 0 keeps all. Before open().
    void setRetention(uint32_t keepNewest);

    /// Roll to a new session file past @p maxBytes; 0 disables. Before open().
    void setMaxFileBytes(uint32_t maxBytes);

    /// Backing-writer flush coalescing. Before open().
    void setFlushInterval(uint32_t ms);

    bool open();
    bool isOpened();
    bool close();

    bool startRecord();

    void append(const char* str);
    void append(bool v);
    void append(uint16_t v);
    void append(int16_t v);
    void append(uint32_t v);
    void append(float value, int precision);

    void appendHex(uint16_t v);

    /**
     * @brief Finalize the record and write it.
     * @param forceFlush When true, flush this record immediately instead of
     *        waiting for the flush interval. Use for rare, important records.
     */
    bool stopRecord(bool forceFlush = false);

private:
    void separator();
    void writeHeader();

    bool        mOpened = false;
    std::string mString;
    const char* mHeader;

    const char* mDir             = nullptr;
    const char* mBase            = nullptr;
    const char* mId              = nullptr;
    uint32_t    mKeepNewest      = 0;
    uint32_t    mMaxFileBytes    = 0;
    uint32_t    mFlushIntervalMs = 0;

    Sink::SessionFileWriter mWriter;
};

} // namespace Component

#endif // COMPONENT_LOGGING_CSV_LOGGER_HPP
