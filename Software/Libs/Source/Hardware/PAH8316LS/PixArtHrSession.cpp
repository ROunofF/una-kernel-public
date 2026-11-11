/**
 ******************************************************************************
 * @file    PixArtHrSession.cpp
 * @brief   Heart-rate session on PixArt's HRD algorithm.
 ******************************************************************************
 */

#include "PixArtHrSession.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace Hardware {

namespace {
/// The sensor's HR register profile has no infrared channel
constexpr uint32_t kIrChannelNum = 0;
/// Touch status the driver reports for no skin contact
constexpr uint8_t kTouchStatusOff = 2;
} // namespace

void PixArtHrSession::process(uint32_t timePeriod, uint32_t *ppgData, const uint8_t *dacData,
                              uint32_t ppgLength, uint8_t channelNum, int16_t *accData,
                              uint32_t accLength, uint8_t isTouched, bool logData,
                              uint8_t &hr, uint8_t &trustLevel)
{
    mData.mems_data          = accData;
    mData.nf_mems            = accLength;
    mData.touch_flag         = (isTouched < kTouchStatusOff) ? 1 : 0;
    mData.time               = timePeriod;
    mData.ppg_data           = reinterpret_cast<int32_t *>(ppgData);
    mData.nf_ppg_channel     = channelNum;
    mData.nf_ppg_per_channel = ppgLength;
    ++mData.frame_count;     // The algorithm uses it to detect lost batches

    if (logData) {
        if (mNeedLogHeader) {
            mNeedLogHeader = false;
            mLog.dataLogStart();
            // Channels, two reserved fields, infrared channels, accelerometer range
            logf("PPG CH#, %d, %d, %d, %d, %d\n", channelNum, 0, 0,
                 static_cast<int>(kIrChannelNum), mGSensorMode);
        }
        logBatch(dacData);
    }

    if (mOpen || open(channelNum)) {
        uint8_t ret = pah8series_entrance(&mData);
        if (ret == MSG_HR_READY) {
            int16_t grade = 0;
            pah8series_get_hr(&mHr);
            pah8series_get_hr_trust_level(&mTrustLevel);
            pah8series_get_signal_grade(&grade);
            logf("myHR = %d, hr_trust_level = %d, grade = %d \n",
                 static_cast<int>(mHr), mTrustLevel, grade);
        }
    }

    hr = static_cast<uint8_t>(mHr);
    trustLevel = static_cast<uint8_t>(mTrustLevel);
}

void PixArtHrSession::close()
{
    if (!mOpen) {
        return;
    }

    mOpen = false;
    pah8series_close();
    std::free(mpAlgBuffer);
    mpAlgBuffer = nullptr;
    mNeedLogHeader = true;
    mHr = 0.0f;
    mTrustLevel = 0;
    mData.frame_count = 0;
    logf("hr_algorithm_close \n");
    mLog.dataLogStop();
}

bool PixArtHrSession::open(uint32_t channelNum)
{
    logf("VERSION= %d \n\r", static_cast<int>(pah8series_version()));

    // On failure, the next batch tries again
    const uint32_t openSize = pah8series_query_open_size();
    mpAlgBuffer = std::malloc(openSize);
    if (mpAlgBuffer == nullptr) {
        logf("pxialg_buffer malloc fail \n\r");
        return false;
    }
    logf("open_size= %d \n\r", static_cast<int>(openSize));

    if (pah8series_open(mpAlgBuffer) != MSG_SUCCESS) {
        logf("pah8series_open fail \n\r");
        std::free(mpAlgBuffer);
        mpAlgBuffer = nullptr;
        return false;
    }

    // Tuning from PixArt
    setParam(PAH8SERIES_PARAM_IDX_GSENSOR_MODE, mGSensorMode);
    setParam(PAH8SERIES_PARAM_IDX_PPG_CH_NUM, channelNum);
    setParam(PAH8SERIES_PARAM_IDX_HAS_IR_CH, kIrChannelNum);
    setParam(PAH8SERIES_PARAM_IDX_SET_FLAG_LIMIT_HR_UB, 1);
    setParam(PAH8SERIES_PARAM_IDX_SET_LIMIT_HR_UB, 220);
    setParam(PAH8SERIES_PARAM_IDX_SET_FLAG_LIMIT_HR_LB, 1);
    setParam(PAH8SERIES_PARAM_IDX_SET_LIMIT_HR_LB, 110);
    setParam(PAH8SERIES_PARAM_IDX_IS_8002, 0);
    setParam(PAH8SERIES_PARAM_IDX_SET_FALG_INIT_HR_LBUB_BY_SIG_QUAL, 1);
    setParam(PAH8SERIES_PARAM_IDX_SET_FLAG_PREDICTION_FEEDBACK, 1);
    setParam(PAH8SERIES_PARAM_IDX_SET_FLAG_PREDICTION_SMOOTH, 1);
    setParam(PAH8SERIES_PARAM_IDX_SET_SMOOTH_TRUST_LEVEL_TH, 1);
    setParam(PAH8SERIES_PARAM_IDX_SET_FLAG_EN_CHANNEL_QUALITY, 1);
    setParam(PAH8SERIES_PARAM_IDX_SET_FLAG_SUPPORT_FFT120, 1);
    setParam(PAH8SERIES_PARAM_IDX_SET_IIR_LP_ALPHA_STATIC, 0.8f);
    setParam(PAH8SERIES_PARAM_IDX_SET_IIR_LP_MOTION_TO_STATIC_COUNT, 40);
    setParam(PAH8SERIES_PARAM_IDX_SET_MOTION_JMP_DIFF_IDX_THR1, 18);

    mOpen = true;
    return true;
}

void PixArtHrSession::setParam(pah8series_param_idx_t param, float value)
{
    pah8series_set_param(param, value);

    // The firmware's printf has no %f: write sign, whole part and five decimals
    const float magnitude = (value >= 0.0f) ? value : -value;
    const int32_t whole = static_cast<int32_t>(magnitude);
    const int32_t fraction = static_cast<int32_t>((magnitude - whole) * 100000);
    logf("Param[%02d] = %c%ld.%05ld \n", static_cast<int>(param), (value >= 0.0f) ? ' ' : '-',
         static_cast<long>(whole), static_cast<long>(fraction));
}

void PixArtHrSession::logBatch(const uint8_t *dacData)
{
    const int sampleNum = static_cast<int>(mData.nf_ppg_channel * mData.nf_ppg_per_channel);
    const uint32_t *ppg = reinterpret_cast<const uint32_t *>(mData.ppg_data);

    logf("Frame Count, %d \n", mData.frame_count);
    logf("Time, %d \n", static_cast<int>(mData.time));

    logf("PPG, %d, %d, ", mData.touch_flag, sampleNum);
    for (int i = 0; i < sampleNum; ++i) {
        logf("%d, ", static_cast<int>(ppg[i]));
    }
    logf("\n");

    logf("MEMS, %d, ", static_cast<int>(mData.nf_mems));
    for (uint32_t i = 0; i < mData.nf_mems * 3; ++i) {
        logf("%d, ", mData.mems_data[i]);
    }
    logf("\n");

    logf("DAC, %d, ", sampleNum);
    for (int i = 0; i < sampleNum; ++i) {
        logf("%d, ", dacData[i]);
    }
    logf("\n");
}

void PixArtHrSession::logf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = std::vsnprintf(mLogLine, sizeof(mLogLine), fmt, args);
    va_end(args);

    if (len <= 0) {
        return;
    }
    if (static_cast<size_t>(len) >= sizeof(mLogLine)) {
        len = sizeof(mLogLine) - 1;
    }
    mLog.dataLogWrite(mLogLine, static_cast<size_t>(len));
}

} // namespace Hardware
