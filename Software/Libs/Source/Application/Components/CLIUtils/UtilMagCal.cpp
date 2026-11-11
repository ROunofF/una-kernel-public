/**
 ******************************************************************************
 * @file    UtilMagCal.cpp
 * @brief   CLI utility that calibrates the magnetometer.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "CLI.MagCal"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "Components/CLIUtils/UtilMagCal.hpp"

#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"

#include "Instances/InstanceMagCal.hpp"

#include <cstring>
#include <cstdlib>

namespace Components::CliUtils {

UtilMagCal::UtilMagCal() :
    UtilBase("magcal"),
    mCommands{
        .execute = UtilMagCal::execute,
        .getName = UtilMagCal::getName,
        .getDesc = UtilMagCal::getDesc,
        .getHelp = UtilMagCal::getHelp,
    }
{
    mReady = mProgress.init("magcalProg") &&
             mDone.init("magcalDone") &&
             mSamples.init("magcalDump");

    if (!mReady) {
        LOG_ERROR("Queues not created; magcal cannot run\n");
    }
}

UtilMagCal& UtilMagCal::getInstance()
{
    static UtilMagCal instance;

    return instance;
}

ICLICommand_t* UtilMagCal::getCommands()
{
    return &mCommands;
}

const char* UtilMagCal::getName(ICLICommand_t* pCmd)
{
    (void) pCmd;

    return UtilMagCal::getInstance().getUtilName();
}

const char* UtilMagCal::getDesc(ICLICommand_t* pCmd)
{
    (void) pCmd;

    return "calibrates the magnetometer";
}

const char* UtilMagCal::getHelp(ICLICommand_t* pCmd)
{
    (void) pCmd;

    CLIConsole_print(
        "magcal <command>\n"
        "\n"
        "  run [seconds]     Collect a tumble, fit it, and store the result.\n"
        "                    Default 30 s. Turn the watch through every\n"
        "                    orientation, including upside down, and keep it\n"
        "                    in one spot: moving it across the room changes\n"
        "                    the field under the measurement.\n"
        "  verify [seconds]  Turn it again and report how much the corrected\n"
        "                    magnitude still moves. Uses the stored\n"
        "                    calibration, so it works after a reboot.\n"
        "                    Default 15 s.\n"
        "  show              Print the correction in force, which after a\n"
        "                    reboot is whatever was stored.\n"
        "  clear             Erase it.\n"
        "  dump [seconds]    Print every sample, one line each: the field as\n"
        "                    measured and the same field with the stored\n"
        "                    correction applied. The measured columns are what\n"
        "                    a fit is built from, so a dump can be re-fitted\n"
        "                    and plotted elsewhere; the corrected ones are\n"
        "                    what the watch navigates by, and their magnitude\n"
        "                    should hold still while it is turned.\n"
        "                    Default 30 s.\n"
        "\n"
        "  seconds, where a command takes them: 15 to 180.\n");

    return "";
}

void UtilMagCal::execute(ICLICommand_t* pCmd, CLIInput_t* pInput,
        int argc, const char** pArgv)
{
    (void) pCmd;
    (void) pInput;

    UtilMagCal& self = UtilMagCal::getInstance();

    if (argc < 2) {
        CLIConsole_print("magcal: expected a command, see 'help magcal'\n");
        return;
    }

    const bool timed = (strcmp(pArgv[1], "run") == 0) ||
                       (strcmp(pArgv[1], "verify") == 0) ||
                       (strcmp(pArgv[1], "dump") == 0);
    const bool plain = (strcmp(pArgv[1], "show") == 0) ||
                       (strcmp(pArgv[1], "clear") == 0);

    if (!timed && !plain) {
        CLIConsole_print("magcal: unknown command '%s'\n", pArgv[1]);
        return;
    }

    // Refused, not adjusted or ignored, so the operator does not misread it.
    if (argc > (timed ? 3 : 2)) {
        if (timed) {
            CLIConsole_print("magcal: %s takes at most one argument, the "
                             "seconds\n", pArgv[1]);
        } else {
            CLIConsole_print("magcal: %s takes no arguments\n", pArgv[1]);
        }
        return;
    }

    uint32_t seconds = 0;
    if (argc == 3) {
        char*               pEnd  = nullptr;
        const unsigned long value = strtoul(pArgv[2], &pEnd, 10);

        if ((pEnd == pArgv[2]) || (*pEnd != '\0') ||
            (value < skMinSec) || (value > skMaxSec)) {
            CLIConsole_print("magcal: seconds must be between %u and %u\n",
                    static_cast<unsigned>(skMinSec),
                    static_cast<unsigned>(skMaxSec));
            return;
        }

        seconds = static_cast<uint32_t>(value);
    }

    if (strcmp(pArgv[1], "run") == 0) {
        self.runCalibrate(orDefault(seconds, skCalibrateDefaultSec));
    } else if (strcmp(pArgv[1], "verify") == 0) {
        self.runVerify(orDefault(seconds, skVerifyDefaultSec));
    } else if (strcmp(pArgv[1], "dump") == 0) {
        self.runDump(orDefault(seconds, skCalibrateDefaultSec));
    } else if (strcmp(pArgv[1], "show") == 0) {
        self.runShow();
    } else {
        self.runClear();
    }
}

uint32_t UtilMagCal::orDefault(uint32_t seconds, uint32_t fallback)
{
    return (seconds == 0U) ? fallback : seconds;
}

void UtilMagCal::onMagCalProgress(const Progress& progress)
{
    // A full queue drops a report; the next one says the same and more.
    (void) mProgress.push(progress, 0);
}

void UtilMagCal::onMagCalSample(uint64_t timestampUs,
        float x, float y, float z)
{
    if (!mDumping.load()) {
        return;
    }

    // Corrected here, so both triples on a line are one sample.
    float cx = x;
    float cy = y;
    float cz = z;

    (void) Instance::MagCal::correction().apply(cx, cy, cz);

    // ms: enough to see a gap, and the console cannot print 64-bit values.
    const Sample sample {
        static_cast<uint32_t>(timestampUs / 1000U), x, y, z, cx, cy, cz
    };

    if (!mSamples.push(sample, 0)) {
        mDropped.fetch_add(1U);
    }
}

void UtilMagCal::onMagCalDone(const Result& result)
{
    Done done;
    done.result = result;

    // Kept on the run's thread, so it is kept even if the console is gone.
    if (mStore.load() && result.fitAccepted()) {
        done.stored = Instance::MagCal::store(result.record);
    }

    // One slot for one run: nothing else is in it.
    (void) mDone.push(done, 0);
}

bool UtilMagCal::begin(Mode mode, uint32_t seconds, bool store, bool dump)
{
    if (!mReady) {
        CLIConsole_print("magcal: not available\n");
        return false;
    }

    Progress progress;
    while (mProgress.pop(progress, 0)) {
    }

    Done done;
    while (mDone.pop(done, 0)) {
    }

    Sample sample;
    while (mSamples.pop(sample, 0)) {
    }

    mDropped.store(0U);
    mPrinted = 0;

    if (!Instance::MagCalRunner::start(*this, mode, seconds)) {
        CLIConsole_print((mode == Mode::CHECK)
                ? "Could not start: no stored calibration, or a run is "
                  "already going\n"
                : "Could not start: a run is already going\n");
        return false;
    }

    // Only after the start is taken, so a refused start leaves a run already
    // going to keep, or not keep, its fit as its own command asked. Nothing
    // is fed before the run's thread connects, and that thread runs below
    // this one.
    mStore.store(store);
    mDumping.store(dump);

    return true;
}

void UtilMagCal::drainSamples()
{
    Sample sample;

    while (mSamples.pop(sample, 0)) {
        CLIConsole_print("%u,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                static_cast<unsigned>(sample.timestampMs),
                sample.x, sample.y, sample.z,
                sample.cx, sample.cy, sample.cz);

        mPrinted++;
    }
}

bool UtilMagCal::await(uint32_t seconds, bool withSpans, Done& done)
{
    const uint32_t startMs = OS::GetTimeMs();
    const uint32_t limitMs = (seconds * 1000U) + skRunGraceMs;
    const bool     dumping = mDumping.load();

    for (;;) {
        Progress progress;

        while (mProgress.pop(progress, 0)) {
            if (dumping) {
                // A dump prints samples; a progress line would break its rows.
                continue;
            }

            if (withSpans) {
                CLIConsole_print(
                        "  %u%%, %u samples, spans %.1f / %.1f / %.1f uT\n",
                        static_cast<unsigned>(progress.percent),
                        static_cast<unsigned>(progress.samples),
                        progress.spanX, progress.spanY, progress.spanZ);
            } else {
                CLIConsole_print("  %u%%, %u samples\n",
                        static_cast<unsigned>(progress.percent),
                        static_cast<unsigned>(progress.samples));
            }
        }

        drainSamples();

        if (mDone.pop(done, 0)) {
            // Every sample was queued before the result was.
            drainSamples();
            mDumping.store(false);
            return true;
        }

        if ((OS::GetTimeMs() - startMs) >= limitMs) {
            mDumping.store(false);
            CLIConsole_print("The run did not report back; stopped waiting\n");
            return false;
        }

        OS::Delay(dumping ? skDumpPollMs : skPollMs);
    }
}

void UtilMagCal::printFit(const Result& result)
{
    const MagCalibration::Record& record = result.record;

    CLIConsole_print("Samples %u, %u of %u directions, "
                     "spans %.1f / %.1f / %.1f uT\n",
            static_cast<unsigned>(result.progress.samples),
            static_cast<unsigned>(result.coverage),
            static_cast<unsigned>(MagCalibrator::skDirections),
            result.progress.spanX, result.progress.spanY,
            result.progress.spanZ);

    if (!result.haveSphere) {
        // No centre placed; the record is the default one.
        CLIConsole_print("No fit from these samples: %s\n",
                MagCalibration::describe(result.verdict));
        return;
    }

    CLIConsole_print("Sphere: radius %.2f uT, residual %.2f uT\n",
            result.sphere.radius, result.sphere.residualRms);

    if (result.haveEllipsoid) {
        CLIConsole_print("Semi-axes %.2f / %.2f / %.2f uT\n",
                result.ellipsoid.semiAxis[0], result.ellipsoid.semiAxis[1],
                result.ellipsoid.semiAxis[2]);
    }

    CLIConsole_print("Offset %.2f / %.2f / %.2f uT, field %.2f uT\n",
            record.offsetX, record.offsetY, record.offsetZ, record.radius);
    CLIConsole_print("Matrix [%.4f %.4f %.4f; %.4f %.4f; %.4f]\n",
            record.m00, record.m01, record.m02,
            record.m11, record.m12, record.m22);
    CLIConsole_print("Verdict: %s\n", MagCalibration::describe(result.verdict));
}

bool UtilMagCal::runCalibrate(uint32_t seconds)
{
    if (!begin(Mode::FIT, seconds, true, false)) {
        return false;
    }

    CLIConsole_print("Turn the watch through every orientation for %u s.\n",
            static_cast<unsigned>(seconds));

    Done done;
    if (!await(seconds, true, done)) {
        return false;
    }

    if (!done.result.completed) {
        CLIConsole_print("The run did not finish\n");
        return false;
    }

    printFit(done.result);

    if (done.result.verdict != MagCalibration::Verdict::OK) {
        return false;
    }

    if (!done.stored) {
        CLIConsole_print("Could not apply or store the calibration\n");
        return false;
    }

    CLIConsole_print("Stored. Run 'magcal verify' to check it on a fresh "
                     "tumble.\n");

    return true;
}

bool UtilMagCal::runVerify(uint32_t seconds)
{
    if (!begin(Mode::CHECK, seconds, false, false)) {
        return false;
    }

    CLIConsole_print("Turn the watch again for %u s.\n",
            static_cast<unsigned>(seconds));

    Done done;
    if (!await(seconds, false, done)) {
        return false;
    }

    if (!done.result.completed) {
        CLIConsole_print("The run did not finish\n");
        return false;
    }

    if (!done.result.haveCheck) {
        CLIConsole_print("No samples collected\n");
        return false;
    }

    const MagCalibrationCheck::Result& check = done.result.check;

    CLIConsole_print("Corrected |B| mean %.2f uT, %.2f .. %.2f uT\n",
            check.mean, check.minimum, check.maximum);
    CLIConsole_print("Corrected spread %.2f uT over %u samples\n",
            check.spread, static_cast<unsigned>(check.samples));
    CLIConsole_print("%s\n", done.result.passed
            ? "The correction holds." : "The correction does not hold.");

    return done.result.passed;
}

void UtilMagCal::runShow()
{
    MagCalibration::Record record;
    if (!Instance::MagCal::read(record)) {
        CLIConsole_print("No stored calibration\n");
        return;
    }

    CLIConsole_print("Offset %.2f / %.2f / %.2f uT, field %.2f uT\n",
            record.offsetX, record.offsetY, record.offsetZ, record.radius);
    CLIConsole_print("Matrix [%.4f %.4f %.4f; %.4f %.4f; %.4f]\n",
            record.m00, record.m01, record.m02,
            record.m11, record.m12, record.m22);
}

void UtilMagCal::runDump(uint32_t seconds)
{
    if (!begin(Mode::FIT, seconds, false, true)) {
        return;
    }

    CLIConsole_print("Turn the watch for %u s, as you would to calibrate.\n",
            static_cast<unsigned>(seconds));
    CLIConsole_print("Microtesla. Correction in force: %s.\n",
            Instance::MagCal::correction().isCalibrated()
                    ? "yes" : "no, the corrected columns repeat the measured");
    CLIConsole_print("ms,x,y,z,cx,cy,cz\n");

    Done       done;
    const bool reported = await(seconds, false, done);

    CLIConsole_print("%u samples", static_cast<unsigned>(mPrinted));

    const uint32_t dropped = mDropped.load();
    if (dropped != 0U) {
        CLIConsole_print(", %u lost to a slow console",
                static_cast<unsigned>(dropped));
    }

    CLIConsole_print("\n");

    if (!reported) {
        return;
    }

    if (!done.result.completed) {
        CLIConsole_print("The run did not finish\n");
        return;
    }

    printFit(done.result);
}

void UtilMagCal::runClear()
{
    if (!Instance::MagCal::clear()) {
        CLIConsole_print("Correction stopped, but the stored calibration could "
                         "not be erased; a restart brings it back\n");
        return;
    }

    CLIConsole_print("Stored calibration erased\n");
}

} // namespace Components::CliUtils
