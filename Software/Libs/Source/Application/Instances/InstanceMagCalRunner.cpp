/**
 ******************************************************************************
 * @file    InstanceMagCalRunner.cpp
 * @brief   The watch's magnetometer calibration run.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "Inst.MagCalRunner"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Instances/InstanceMagCalRunner.hpp"

#include <algorithm>
#include <atomic>
#include <new>

#include "cmsis_os2.h"
#include "OS.hpp"

#include "Instances/InstanceMagCal.hpp"
#include "SensorLayer/Core/SensorConnection.hpp"
#include "SDK/Interfaces/ISensorDataListener.hpp"
#include "SDK/SensorLayer/SensorDataBatch.hpp"
#include "SDK/SensorLayer/DataParsers/SensorDataParserMagneticFieldRaw.hpp"

namespace Instance {

namespace {

const char kCancelled = 'c';
const char kEnding    = 'e';

/// Who the run belongs to, or how it is ending. Not null from start() until
/// the run's thread has deleted its Runner, which keeps that Runner the only
/// one.
///   nullptr     no run
///   &owner      running for that owner
///   &kCancelled its owner asked it to end
///   &kEnding    ending on its own; a cancel is too late
std::atomic<const void*> sSlot {nullptr};

/// One run: its session, its subscription and its thread.
class Runner final : public SDK::Interface::ISensorDataListener
{
public:
    Runner(MagCalRunner::Owner& owner, uint32_t totalMs)
        : mOwner(owner)
        , mTotalMs(totalMs)
        , mConnection(SDK::Sensor::Type::MAGNETIC_FIELD_RAW, this,
                      skPeriodMs, skLatencyMs)
    {
    }

    MagCalibrationSession& session()
    {
        return mSession;
    }

    /// A fit spread over the samples this run's length and rate give.
    void startFit()
    {
        mSession.startFit(static_cast<uint32_t>(
                static_cast<float>(mTotalMs) / skPeriodMs));
    }

    /// Thread entry. Frees the Runner and ends the thread.
    static void entry(void* arg);

    /// Carries the fit, the result and the owner's onMagCalDone(): about
    /// 1.3 KB measured on hardware, the rest is margin.
    static constexpr uint32_t skStackBytes = 3 * 1024;

private:
    void run();
    void report(uint32_t elapsedMs);

    void onSdlNewData(uint16_t handle, const SDK::Sensor::Data* base,
                      uint16_t count, uint16_t stride) override;

    /// 25 Hz; the rate only sets how fast a tumble accumulates.
    static constexpr float skPeriodMs = 40.0f;

    static constexpr uint32_t skLatencyMs = 0;

    /// How often a cancel is looked for; it answers a button press.
    static constexpr uint32_t skPollMs = 50;

    /// Progress every this many polls: half a second.
    static constexpr uint32_t skPollsPerReport = 10;

    MagCalRunner::Owner&  mOwner;
    const uint32_t        mTotalMs;
    MagCalibrationSession mSession;
    Sensor::Connection    mConnection;
};

void Runner::entry(void* arg)
{
    Runner* pRunner = static_cast<Runner*>(arg);

    pRunner->run();

    delete pRunner;

    // After the Runner is gone, so that a new one cannot exist beside it.
    sSlot.store(nullptr);

    osThreadExit();
}

void Runner::run()
{
    const void* const owner     = &mOwner;
    bool              completed = false;

    if (!mConnection.connect()) {
        LOG_WARNING("No magnetic field to calibrate from\n");
    } else {
        const uint32_t startMs = OS::GetTimeMs();

        for (uint32_t poll = 0; ; poll++) {
            if (sSlot.load() == &kCancelled) {
                break;
            }

            const uint32_t elapsedMs = OS::GetTimeMs() - startMs;

            if (elapsedMs >= mTotalMs) {
                // One step against a cancel landing now: exactly one wins.
                const void* expected = owner;
                completed = sSlot.compare_exchange_strong(expected, &kEnding);
                break;
            }

            if ((poll % skPollsPerReport) == 0U) {
                report(elapsedMs);
            }

            OS::Delay(skPollMs);
        }

        // Synchronous with delivery: once this returns no sample is being fed,
        // and the session is this thread's alone.
        mConnection.disconnect();
    }

    // A cancel from here on is too late, and is told so.
    const void* expected = owner;
    (void) sSlot.compare_exchange_strong(expected, &kEnding);

    const MagCalibrationSession::Result result = mSession.finish(completed);

    // Logged in case nobody is left to print the result.
    if (!completed) {
        LOG_INFO("Run ended early\n");
    } else if (result.mode == MagCalibrationSession::Mode::FIT) {
        LOG_INFO("Fit ended at its deadline: %s, %u samples, "
                 "%u of %u directions\n",
                 MagCalibration::describe(result.verdict),
                 static_cast<unsigned>(result.progress.samples),
                 static_cast<unsigned>(result.coverage),
                 static_cast<unsigned>(MagCalibrator::skDirections));
    } else {
        LOG_INFO("Check ended at its deadline: %s, %u samples\n",
                 result.passed ? "the correction holds"
                               : "the correction does not hold",
                 static_cast<unsigned>(result.progress.samples));
    }

    mOwner.onMagCalDone(result);

    LOG_DEBUG("%u bytes of the run's stack never used\n",
             static_cast<unsigned>(osThreadGetStackSpace(osThreadGetId())));
}

void Runner::report(uint32_t elapsedMs)
{
    MagCalRunner::Progress progress;
    static_cast<MagCalibrator::Progress&>(progress) = mSession.getProgress();
    progress.percent = static_cast<uint8_t>(
            std::min<uint32_t>(100U, (elapsedMs * 100U) / mTotalMs));

    mOwner.onMagCalProgress(progress);
}

void Runner::onSdlNewData(uint16_t handle, const SDK::Sensor::Data* base,
                          uint16_t count, uint16_t stride)
{
    (void) handle;

    SDK::Sensor::DataBatch batch(base, count, stride);

    for (uint16_t idx = 0; idx < batch.size(); ++idx) {
        const SDK::SensorDataParser::MagneticFieldRaw measured(batch[idx]);

        const float x = measured.getX();
        const float y = measured.getY();
        const float z = measured.getZ();

        mSession.feed(x, y, z);
        mOwner.onMagCalSample(measured.getTimestampUs(), x, y, z);
    }
}

} // namespace

bool MagCalRunner::start(Owner& owner, Mode mode, uint32_t maxSeconds)
{
    if (maxSeconds == 0U) {
        return false;
    }

    maxSeconds = std::min(maxSeconds, skMaxSeconds);

    const void* expected = nullptr;
    if (!sSlot.compare_exchange_strong(expected, &owner)) {
        LOG_INFO("Refused: a run is already going\n");
        return false;
    }

    Runner* pRunner = new (std::nothrow) Runner(owner, maxSeconds * 1000U);
    bool    ready   = (pRunner != nullptr);

    if (ready) {
        if (mode == Mode::FIT) {
            pRunner->startFit();
        } else {
            MagCalibration::Record record;
            ready = MagCal::read(record) && pRunner->session().startCheck(record);
        }
    }

    if (ready) {
        osThreadAttr_t attr {};
        attr.name       = "MagCalRun";
        attr.stack_size = Runner::skStackBytes;
        attr.priority   = osPriorityBelowNormal;

        ready = (osThreadNew(&Runner::entry, pRunner, &attr) != nullptr);
    }

    if (!ready) {
        delete pRunner;
        sSlot.store(nullptr);

        LOG_WARNING("Refused: nothing to check, or no memory for a run\n");
        return false;
    }

    LOG_INFO("%s started for up to %u s\n",
             (mode == Mode::FIT) ? "Fit" : "Check",
             static_cast<unsigned>(maxSeconds));

    return true;
}

bool MagCalRunner::cancel(const Owner& owner)
{
    const void* expected = &owner;

    return sSlot.compare_exchange_strong(expected, &kCancelled);
}

} // namespace Instance
