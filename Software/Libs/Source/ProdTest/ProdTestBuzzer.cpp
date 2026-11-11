/**
 ******************************************************************************
 * @file    ProdTestBuzzer.сpp
 * @brief   Production test for the Buzzer.
 ******************************************************************************
 *
 ******************************************************************************
 */

#define LOG_MODULE_PRX      "ProdTest.Buzzer"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger/Logger.h"

#include "ProdTest/ProdTestBuzzer.hpp"
#include "CLI/CLIConsole.h"
#include "Buzzer.hpp"
#include "Gpio.hpp"
#include "Pwm.hpp"
#include "OS.hpp"

#include <cstdio>
#include <cstdlib>

namespace ProdTest
{

namespace
{

// Parse a decimal argument into [minVal, maxVal]; false if it isn't one.
bool parseArg(const char* s, long minVal, long maxVal, long& out)
{
    char* end = nullptr;
    const long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < minVal || v > maxVal) {
        return false;
    }
    out = v;
    return true;
}

// Optional trailing arguments: [level 1-3] [toneMs] [gapMs].
bool parseToneArgs(int argc, const char** argv, int first,
                   long& level, long& toneMs, long& gapMs)
{
    if (argc > first     && !parseArg(argv[first],     1, 3,     level))  return false;
    if (argc > first + 1 && !parseArg(argv[first + 1], 10, 5000, toneMs)) return false;
    if (argc > first + 2 && !parseArg(argv[first + 2], 0, 5000,  gapMs))  return false;
    return true;
}

bool playAt(long hz, long level, long toneMs)
{
    Interface::IBuzzer::Note note = {
        .time      = static_cast<uint32_t>(toneMs),
        .level     = static_cast<uint8_t>(level),
        .frequency = static_cast<uint16_t>(hz)
    };
    return ::Buzzer::GetInstance().play(note);
}

} // namespace

Buzzer::Buzzer(const std::string& id) : Interface::IProdTest(id)
{}

bool Buzzer::run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input)
{
    LOG_INFO_WP("\n");
    LOG_INFO("Test Buzzer (PAM8904)...\n");

    ::Buzzer &buzzer = ::Buzzer::GetInstance();

    buzzer.config( Driver::Pwm::GetInstance(Driver::Pwm::Port::PORT_1),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::BUZZER_EN1),
            Driver::Gpio::GetInstance(Driver::Gpio::Pin::BUZZER_EN2));

    buzzer.init();
    buzzer.enable();

    CLIConsole_print(CLI_UTIL_PROMPT);

    while (true) {
        OS::Delay(10);

        if (CLIInput_refresh(input) == true) {
            if (strcmp(CLIInput_getArgV(input)[0], "exit") == 0) {
                break;
            }

            checkCommand(slog, input);

            CLIInput_reset(input);
            CLIConsole_print(CLI_UTIL_PROMPT);
        }
    }

    buzzer.disable();
    buzzer.deinit();

    return true;
}

void Buzzer::startBuzzer(StringLogger& slog, int argc, const char** argv)
{
    if (argc != 2) {
        CLIConsole_print(CLI_INVALID_PARAM_NUMBER_MSG);
        return;
    }

    uint8_t level;
    if (strcmp(argv[1], "1") == 0) {
        level = 1;
    } else if (strcmp(argv[1], "2") == 0) {
        level = 2;
    } else if (strcmp(argv[1], "3") == 0) {
        level = 3;
    } else {
        CLIConsole_print(CLI_INVALID_PARAM_MSG);
        return;
    }

    ::Buzzer& buzzer = ::Buzzer::GetInstance();

    Interface::IBuzzer::Note note = {
        .time  = 1000,
        .level = level
    };

    buzzer.play(note);
}

// freq <Hz> [level 1-3] [ms]: one tone, default level 3 for 1000 ms.
void Buzzer::playTone(int argc, const char** argv)
{
    long hz     = 0;
    long level  = 3;
    long toneMs = 1000;
    long gapMs  = 0;

    if (argc < 2 || argc > 4) {
        CLIConsole_print(CLI_INVALID_PARAM_NUMBER_MSG);
        return;
    }
    if (!parseArg(argv[1], 500, 20000, hz) ||
        !parseToneArgs(argc, argv, 2, level, toneMs, gapMs)) {
        CLIConsole_print(CLI_INVALID_PARAM_MSG);
        return;
    }

    if (!playAt(hz, level, toneMs)) {
        CLIConsole_print("tone failed\n");
        return;
    }
    OS::Delay(toneMs);
    CLIConsole_print("tone %ld Hz level %ld %ld ms\n", hz, level, toneMs);
}

// sweep <fromHz> <toHz> <stepHz> [level 1-3] [toneMs] [gapMs]: one tone per
// step, default level 3, 500 ms on, 300 ms off. Blocks until done.
void Buzzer::sweep(int argc, const char** argv)
{
    long fromHz = 0;
    long toHz   = 0;
    long stepHz = 0;
    long level  = 3;
    long toneMs = 500;
    long gapMs  = 300;

    if (argc < 4 || argc > 7) {
        CLIConsole_print(CLI_INVALID_PARAM_NUMBER_MSG);
        return;
    }
    if (!parseArg(argv[1], 500, 20000, fromHz) ||
        !parseArg(argv[2], 500, 20000, toHz)   ||
        !parseArg(argv[3], 1, 20000, stepHz)   ||
        fromHz > toHz ||
        !parseToneArgs(argc, argv, 4, level, toneMs, gapMs)) {
        CLIConsole_print(CLI_INVALID_PARAM_MSG);
        return;
    }

    CLIConsole_print("sweep %ld..%ld Hz step %ld, level %ld, %ld ms on, %ld ms off\n",
                     fromHz, toHz, stepHz, level, toneMs, gapMs);

    for (long hz = fromHz; hz <= toHz; hz += stepHz) {
        if (!playAt(hz, level, toneMs)) {
            CLIConsole_print("tone %ld Hz failed\n", hz);
            break;
        }
        CLIConsole_print("%ld\n", hz);
        OS::Delay(toneMs + gapMs);
    }

    CLIConsole_print("sweep done\n");
}

// play <Hz>:<ms>[:<level 1-3>] ...: a melody of up to skMaxNotes notes, as an
// app would send it. 0 Hz is a silence; the level defaults to 3. Blocks until
// it has played.
void Buzzer::playNotes(int argc, const char** argv)
{
    if (argc < 2 || argc - 1 > Interface::IBuzzer::skMaxNotes) {
        CLIConsole_print(CLI_INVALID_PARAM_NUMBER_MSG);
        return;
    }

    Interface::IBuzzer::Note melody[Interface::IBuzzer::skMaxNotes];
    uint32_t totalMs = 0;
    for (int i = 1; i < argc; ++i) {
        char hz[8]  = {};
        char ms[8]  = {};
        char lvl[4] = "3";
        long hzVal = 0, msVal = 0, lvlVal = 0;
        // Fields are split by ':'; a field too long for its buffer, or
        // anything left over after the last field, is invalid.
        int end = 0;
        bool whole = sscanf(argv[i], "%7[0-9]:%7[0-9]%n", hz, ms, &end) == 2;
        if (whole && argv[i][end] == ':') {
            int lvlEnd = 0;
            whole = sscanf(argv[i] + end + 1, "%3[0-9]%n", lvl, &lvlEnd) == 1;
            end += 1 + lvlEnd;
        }
        if (!whole || argv[i][end] != '\0' ||
            !parseArg(hz, 0, 20000, hzVal) || (hzVal != 0 && hzVal < 500) ||
            !parseArg(ms, 1, 5000, msVal) || !parseArg(lvl, 1, 3, lvlVal)) {
            CLIConsole_print("bad note '%s': use Hz:ms[:level], 0 Hz for a silence\n", argv[i]);
            return;
        }
        Interface::IBuzzer::Note& n = melody[i - 1];
        n.time      = static_cast<uint32_t>(msVal);
        n.level     = (hzVal == 0) ? 0 : static_cast<uint8_t>(lvlVal);
        n.frequency = static_cast<uint16_t>(hzVal);
        totalMs += n.time;
    }

    // A silence keeps the pitch around it, as the apps send it, so the timer is
    // not retuned for nothing.
    for (int i = 0; i < argc - 1; ++i) {
        if (melody[i].frequency == 0) {
            melody[i].frequency = (i > 0) ? melody[i - 1].frequency : 0;
        }
    }

    if (!::Buzzer::GetInstance().play(melody, static_cast<uint8_t>(argc - 1))) {
        CLIConsole_print("play failed\n");
        return;
    }
    OS::Delay(totalMs);
    CLIConsole_print("played %d notes, %lu ms\n", argc - 1, static_cast<unsigned long>(totalMs));
}

// alert <slow|fast|back>: one playing of a workout zone alert, as Running
// sends it (the app plays each twice, about 1 s apart).
void Buzzer::playAlert(int argc, const char** argv)
{
    static const char* const kSlow[] = {"play", "2093:300", "0:150", "1047:300", "0:150",
                                        "2093:300", "0:150", "1047:300"};
    static const char* const kFast[] = {"play", "4186:80", "0:80", "8372:80", "0:80",
                                        "4186:80", "0:80", "8372:80"};
    static const char* const kBack[] = {"play", "2093:120", "0:30", "2616:120", "0:30", "3140:240"};

    if (argc != 2) {
        CLIConsole_print(CLI_INVALID_PARAM_NUMBER_MSG);
        return;
    }
    if (strcmp(argv[1], "slow") == 0) {
        playNotes(sizeof(kSlow) / sizeof(kSlow[0]), const_cast<const char**>(kSlow));
    } else if (strcmp(argv[1], "fast") == 0) {
        playNotes(sizeof(kFast) / sizeof(kFast[0]), const_cast<const char**>(kFast));
    } else if (strcmp(argv[1], "back") == 0) {
        playNotes(sizeof(kBack) / sizeof(kBack[0]), const_cast<const char**>(kBack));
    } else {
        CLIConsole_print("alert slow, fast or back\n");
    }
}

void Buzzer::checkCommand(StringLogger& slog, CLIInput_t* input)
{
    int          argc = CLIInput_getArgC(input);
    const char** argv = CLIInput_getArgV(input);

    if (strcmp(argv[0], "level") == 0) {
        startBuzzer(slog, argc, argv);
    } else if (strcmp(argv[0], "freq") == 0) {
        playTone(argc, argv);
    } else if (strcmp(argv[0], "sweep") == 0) {
        sweep(argc, argv);
    } else if (strcmp(argv[0], "play") == 0) {
        playNotes(argc, argv);
    } else if (strcmp(argv[0], "alert") == 0) {
        playAlert(argc, argv);
    } else {
        if (argc != 0) {
            CLIConsole_print("unknown command\n");
        }
    }
}

}   // namespace ProdTest
