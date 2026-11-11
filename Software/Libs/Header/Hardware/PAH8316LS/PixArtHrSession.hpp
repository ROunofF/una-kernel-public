/**
 ******************************************************************************
 * @file    PixArtHrSession.hpp
 * @brief   Heart-rate session on PixArt's HRD algorithm: opens the algorithm,
 *          feeds it batches of PPG and accelerometer samples, and reads back
 *          the heart rate.
 ******************************************************************************
 */

#ifndef PIXART_HR_SESSION_HPP
#define PIXART_HR_SESSION_HPP

#include <cstddef>
#include <cstdint>

extern "C" {
#include "pah8series_api_c.h"
}

namespace Hardware {

class PixArtHrSession
{
public:
    /**
     * Destination of the raw-data log: the samples and results of a session,
     * in the text format PixArt's analysis tools read.
     */
    class DataLog
    {
    public:
        /// A session with data logging enabled has produced its first batch
        virtual void dataLogStart() = 0;
        virtual void dataLogWrite(const char *data, size_t size) = 0;
        /// The session closed
        virtual void dataLogStop() = 0;

    protected:
        ~DataLog() = default;
    };

    explicit PixArtHrSession(DataLog &log) : mLog(log) {}

    /// Accelerometer full scale of the samples passed to process(): 0 = 2 g, 1 = 4 g, 2 = 8 g
    void setGSensorMode(uint8_t mode) { mGSensorMode = mode; }
    uint8_t gSensorMode() const { return mGSensorMode; }

    /**
     * @brief   Run the algorithm on one batch. The first batch after close()
     *          opens a new session.
     * @param   timePeriod  Length of the batch, ms
     * @param   ppgData     channelNum * ppgLength samples, channels interleaved
     * @param   dacData     One LED DAC value per sample, for the data log
     * @param   accData     accLength x, y, z triples
     * @param   isTouched   Touch status from the driver: 2 means no contact
     * @param   logData     Write this batch to the data log
     * @param   hr          Heart rate, bpm; 0 until the algorithm has one
     * @param   trustLevel  The algorithm's confidence in hr
     */
    void process(uint32_t timePeriod, uint32_t *ppgData, const uint8_t *dacData,
                 uint32_t ppgLength, uint8_t channelNum, int16_t *accData,
                 uint32_t accLength, uint8_t isTouched, bool logData,
                 uint8_t &hr, uint8_t &trustLevel);

    /// End the session, if one is open, and release the algorithm's memory
    void close();

private:
    static constexpr size_t skLogLineLength = 256;

    DataLog &mLog;
    uint8_t mGSensorMode = 2;

    void *mpAlgBuffer = nullptr;
    pah8series_data_t mData {};
    bool mOpen = false;
    bool mNeedLogHeader = true;
    float mHr = 0.0f;
    int mTrustLevel = 0;
    /// Formatting buffer for the data log, kept off the sensor thread's stack
    char mLogLine[skLogLineLength];

    bool open(uint32_t channelNum);
    void setParam(pah8series_param_idx_t param, float value);
    void logBatch(const uint8_t *dacData);
    void logf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

} // namespace Hardware

#endif // PIXART_HR_SESSION_HPP
