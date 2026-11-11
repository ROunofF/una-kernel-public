/**
 ******************************************************************************
 * @file    SessionNaming.hpp
 * @brief   Session-file naming + retention policy (the "where/what name" half).
 *
 * Owns the <dir><base>_<id>_<NNNN>_<ts>.<ext> contract and the count-based
 * retention (via SessionFiles::prepare). It produces paths; it does not write
 * bytes - hand its nextPath() to an AsyncFileWriter as the rollover namer.
 ******************************************************************************
 */

#ifndef SINK_SESSION_NAMING_HPP
#define SINK_SESSION_NAMING_HPP

#include <cstddef>
#include <cstdint>
#include <string>

namespace Sink {

/**
 * @brief Names session files and prunes old ones; the policy layer over a writer.
 */
class SessionNaming {
public:
    /**
     * @brief Configure naming and retention.
     * @param dir        Directory, e.g. "2:/BatteryLog/".
     * @param base       Logger base name, e.g. "battery".
     * @param id         Device id string.
     * @param ext        Extension without the dot, e.g. "csv".
     * @param keepNewest Session files to keep incl. the live one; 0 = keep all.
     */
    void configure(const char* dir, const char* base, const char* id,
                   const char* ext, uint32_t keepNewest);

    /**
     * @brief Capture the session start timestamp, shared by every segment of the
     *        session so rolled files stay visibly one session. Call once before
     *        the first nextPath().
     */
    void captureSession();

    /**
     * @brief Build the next segment path (next NNNN + prune) into @p out.
     * @return true on success.
     */
    bool nextPath(char* out, size_t outSize);

    /** @brief Adapter matching AsyncFileWriter::NextPathFn. */
    static bool nextPathTrampoline(void* ctx, char* out, size_t outSize)
    {
        return static_cast<SessionNaming*>(ctx)->nextPath(out, outSize);
    }

private:
    std::string mDir;
    std::string mPrefix;             // "<base>_<id>_"
    std::string mSuffix;             // ".<ext>"
    uint32_t    mKeepNewest    = 0;
    char        mSessionTs[16] = {}; // YYYYMMDDTHHMMSS
};

}  // namespace Sink

#endif  // SINK_SESSION_NAMING_HPP
