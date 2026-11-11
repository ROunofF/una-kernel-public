/**
 ******************************************************************************
 * @file    UtilMagCal.hpp
 * @brief   CLI utility that calibrates the magnetometer.
 *
 *          A front end only. The CLI thread can be terminated mid-command, so
 *          it holds no lock across a wait: a callback does its work on the
 *          run's thread and pushes the outcome without waiting, and this
 *          thread pops it.
 ******************************************************************************
 */

#ifndef UTIL_MAG_CAL_HPP
#define UTIL_MAG_CAL_HPP

#include <atomic>
#include <cstdint>

#include <Porting.h>

#include "CLI/UtilBase.hpp"
#include "CLI/ICommand.h"

#include "Instances/InstanceMagCalRunner.hpp"
#include "OS.hpp"

namespace Components {

namespace CliUtils {

class UtilMagCal : public UtilBase,
                   public Instance::MagCalRunner::Owner
{

public:

    static UtilMagCal& getInstance();

    UtilMagCal(UtilMagCal const&)     = delete;
    void operator=(UtilMagCal const&) = delete;

    UtilMagCal(UtilMagCal&&)            = delete;
    UtilMagCal& operator=(UtilMagCal&&) = delete;

    ICLICommand_t* getCommands() override;

private:

    using Mode     = Instance::MagCalRunner::Mode;
    using Progress = Instance::MagCalRunner::Progress;
    using Result   = Instance::MagCalRunner::Result;

    /// A finished run, and whether its fit was kept.
    struct Done {
        Result result;
        bool   stored = false;
    };

    /// One sample as measured and as the correction in force leaves it.
    struct Sample {
        uint32_t timestampMs;
        float    x;
        float    y;
        float    z;
        float    cx;
        float    cy;
        float    cz;
    };

    static constexpr uint32_t skCalibrateDefaultSec = 30;
    static constexpr uint32_t skVerifyDefaultSec    = 15;
    static constexpr uint32_t skMinSec              = 15;   ///< enough samples for a fit
    static constexpr uint32_t skMaxSec              = 180;

    static constexpr uint32_t skPollMs = 50;

    /// How often a dump looks for samples; well under the sample period.
    static constexpr uint32_t skDumpPollMs = 5;

    /// Grace past a run's length before this stops waiting; the run still
    /// ends itself.
    static constexpr uint32_t skRunGraceMs = 3000;

    static constexpr uint32_t skDumpSlots = 64;

    ICLICommand_t mCommands;

    explicit UtilMagCal();
    ~UtilMagCal() override = default;

    static const char* getName(ICLICommand_t* pCmd);
    static const char* getDesc(ICLICommand_t* pCmd);
    static const char* getHelp(ICLICommand_t* pCmd);
    static void        execute(ICLICommand_t* pCmd,
                               CLIInput_t*    pInput,
                               int            argc,
                               const char**   pArgv);

    //// Instance::MagCalRunner::Owner
    void onMagCalProgress(const Progress& progress) override;
    void onMagCalSample(uint64_t timestampUs,
                        float x, float y, float z) override;
    void onMagCalDone(const Result& result) override;

    bool runCalibrate(uint32_t seconds);
    bool runVerify(uint32_t seconds);
    void runShow();
    void runClear();

    /// Print every sample as measured and as corrected, then the fit.
    /// Nothing is kept.
    void runDump(uint32_t seconds);

    /// Start a run with whatever an earlier one left in the queues discarded.
    /// @param store Keep the fit if the run completes with verdict OK.
    /// @param dump  Queue samples for printing.
    bool begin(Mode mode, uint32_t seconds, bool store, bool dump);

    /// Print progress, and samples when dumping, until the run reports.
    /// @retval 'false' if it did not report within its length and a grace.
    bool await(uint32_t seconds, bool withSpans, Done& done);

    void drainSamples();

    static void printFit(const Result& result);

    static uint32_t orDefault(uint32_t seconds, uint32_t fallback);

    /// All queues created. Without them no run is started.
    bool mReady = false;

    OS::Queue<Progress, 4>          mProgress;
    OS::Queue<Done, 1>              mDone;
    OS::Queue<Sample, skDumpSlots>  mSamples;

    std::atomic<bool>     mStore   {false};
    std::atomic<bool>     mDumping {false};
    std::atomic<uint32_t> mDropped {0};

    /// Samples printed by the dump going. CLI thread only.
    uint32_t mPrinted = 0;
};

} // namespace CliUtils
} // namespace Components

#endif // UTIL_MAG_CAL_HPP
