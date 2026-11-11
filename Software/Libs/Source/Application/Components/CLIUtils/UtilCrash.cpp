/**
 *******************************************************************************
 * @file    UtilCrash.cpp
 * @brief   A CLI utility that allows you to crash a device. This is used to
 *          test the CrashDump component.
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "Components/CLIUtils/UtilCrash.hpp"
#include "CLI/ICommand.h"
#include "CLI/CLIConsole.h"
#include "OS.hpp"
#include "main.h"   // CMSIS NVIC_* and TAMP_IRQn for the in-ISR crash test

#include <cstring>

/**
 * @brief Selects what TAMP_IRQHandler does: fault (`crash irq`) or recurse
 *        until the main stack overflows (`crash msp`).
 */
static volatile bool sTampRecurse = false;

/**
 * @brief Sink that keeps the recursion frame live past the call (defeats
 *        tail-call elimination) so the stack actually grows.
 */
static volatile uint32_t sStackGuard;

/**
 * @brief Recurse on whichever stack is current, without yielding.
 *
 * Called from TAMP_IRQHandler, so "current" is MSP. There is deliberately no
 * OS::Delay here - the task-stack version yields so FreeRTOS's software check
 * runs on the next switch, but this one is testing the HARDWARE guard, which
 * fires on the offending push itself and needs no cooperation from anything.
 */
static void mspRecurse(uint32_t depth)
{
    // With 1024 usable bytes and ~72 per frame, MSPLIM fires around depth 14.
    // Reaching this bound means it did not - unarmed, or a misconfigured
    // _Min_Stack_Size. The recursion is then walking down through the image
    // region and the kernel heap, so stopping bounds the damage at roughly
    // 2 KB past the region rather than preventing it. That is enough to leave
    // the watch able to report a negative result instead of dying somewhere
    // unrelated, which is all this bound is for.
    if (depth > 32u) {
        return;
    }

    // Never true at runtime (volatile read -> the compiler cannot prove it),
    // but it gives a return path so -Winfinite-recursion stays quiet.
    if (sStackGuard == 0xDEADBEEFu) {
        return;
    }

    volatile uint8_t frame[64];
    frame[0]  = (uint8_t)depth;
    frame[63] = frame[0];

    mspRecurse(depth + 1);

    sStackGuard += frame[63];           // use frame after the call -> not a tail call
}

/**
 * @brief Spare-IRQ handler shared by `crash irq` and `crash msp`.
 *
 * TAMP (tamper) is otherwise unused in this project, so its weak vector is
 * overridden here. Running in it gives a crash in handler mode - the exception
 * frame is stacked on MSP - which is the path the thread-mode `crash` cases
 * never reach, and the only context in which the MAIN stack can be overflowed
 * at all. sTampRecurse picks which of the two it does. The IRQ is enabled only
 * by those two commands, so there is no effect in normal operation.
 */
extern "C" void TAMP_IRQHandler(void)
{
    if (sTampRecurse) {
        // Walk MSP down past MSPLIM. The guard should trap the push that
        // crosses it, raising a UsageFault with STKOF set which escalates to
        // HardFault - a deterministic, attributable crash instead of a silent
        // write into whatever sits below the main stack.
        mspRecurse(0);
        return;
    }

    volatile uint32_t *p = (uint32_t *)0xFFFFFFFFu;
    *p = 0x12345678u;
}

/**
 * @brief Recurse until the task stack overflows.
 *
 * Each frame holds a buffer and yields, so the FreeRTOS stack-overflow check
 * (configCHECK_FOR_STACK_OVERFLOW) fires on the next context switch.
 */
static void crashStackOverflow(uint32_t depth)
{
    // Never true at runtime (volatile read -> the compiler cannot prove it),
    // but it gives a return path so -Winfinite-recursion stays quiet.
    if (sStackGuard == 0xDEADBEEFu) {
        return;
    }

    volatile uint8_t frame[64];
    frame[0]  = (uint8_t)depth;
    frame[63] = frame[0];

    OS::Delay(1);                       // yield so the overflow check runs on switch
    crashStackOverflow(depth + 1);

    sStackGuard += frame[63];           // use frame after the call -> not a tail call
}

static void crashPrintUsage(void)
{
    CLIConsole_print("Usage: crash <type> - crash the device to test CrashDump\n");
    CLIConsole_print("  hardfault - invalid memory access (HardFault)\n");
    CLIConsole_print("  assert    - failed assert (ASSERT)\n");
    CLIConsole_print("  stack     - task stack overflow (STACK_OVERFLOW)\n");
    CLIConsole_print("  msp       - main stack overflow (MSPLIM guard -> HardFault)\n");
    CLIConsole_print("  watchdog  - hang with IRQs on (IWDG early interrupt -> WATCHDOG)\n");
    CLIConsole_print("  hang      - hang with IRQs off (hard watchdog reset -> synthesized dump)\n");
    CLIConsole_print("  irq       - crash inside an ISR (handler-mode / MSP capture)\n");
}

/**
 * @brief Construct a new Components::CliUtils::UtilCrash object
 * 
 */
Components::CliUtils::UtilCrash::UtilCrash() :
        UtilBase("crash"),
        mCommands{
            .execute = UtilCrash::execute,
            .getName = UtilCrash::getName,
            .getDesc = UtilCrash::getDesc,
            .getHelp = UtilCrash::getHelp,
        }
{

}

/**
 * @brief  Returns the reference to the single instance of the Crash CLI utility
 *
 * @return A reference to the utility object
 */
Components::CliUtils::UtilCrash& Components::CliUtils::UtilCrash::getInstance()
{
    static UtilCrash instance;
    return instance;
}

/**
 * @brief  Returns a pointer to the structure with pointers to static command
 *         handlers
 * 
 * @return A pointer to the ICLICommand_t
 */
ICLICommand_t *Components::CliUtils::UtilCrash::getCommands()
{
    return &mCommands;
}

/**
 * @brief  Returns the utility name
 * 
 * @param  pCmd: A pointer to the command handler functions
 * 
 * @return Utility name
 */
const char* Components::CliUtils::UtilCrash::getName(ICLICommand_t* cmd)
{
    return UtilCrash::getInstance().getUtilName();
}

/**
 * @brief  Returns a brief utility description
 * 
 * @param  pCmd: A pointer to the command handler functions
 * 
 * @return Utility description
 */
const char* Components::CliUtils::UtilCrash::getDesc(ICLICommand_t* cmd)
{
    return "test of the crash dump component";
}

/**
 * @brief  Provides a help on how to use the utility
 * 
 * @param  pCmd: A pointer to the command handler functions
 * 
 * @return Empty ""
 */
const char* Components::CliUtils::UtilCrash::getHelp(ICLICommand_t* cmd)
{
    (void) cmd;
    crashPrintUsage();

    return "";
}

/**
 * @brief  Crash the device through the selected fault path (see crashPrintUsage).
 *
 * @param  cmd:   Pointer to the command handler functions
 * @param  input: Pointer to the CLI input
 * @param  argc:  Count of the arguments (argv[1] is the crash type)
 * @param  argv:  Pointer to the arguments
 */
void Components::CliUtils::UtilCrash::execute(ICLICommand_t* cmd,
                                              CLIInput_t*    input,
                                              int            argc,
                                              const char**   argv)
{
    (void) cmd;
    (void) input;

    const char* type = (argc >= 2) ? argv[1] : "";

    if (strcmp(type, "hardfault") == 0) {
        // Invalid memory access -> HardFault.
        volatile uint32_t *p = (uint32_t *)0xFFFFFFFFu;
        *p = 0x12345678u;
    } else if (strcmp(type, "assert") == 0) {
        // Failed assert -> CrashDump ASSERT path.
        assert(false);
    } else if (strcmp(type, "stack") == 0) {
        // Grow the task stack until the overflow hook fires.
        crashStackOverflow(0);
    } else if (strcmp(type, "watchdog") == 0) {
        // Hang with interrupts enabled: the IWDG early interrupt fires (WATCHDOG).
        for (;;) {}
    } else if (strcmp(type, "hang") == 0) {
        // Hang with interrupts disabled: the early interrupt cannot fire, so the
        // IWDG hard-resets the chip and the dump is synthesized on next boot.
        __disable_irq();
        for (;;) {}
    } else if (strcmp(type, "msp") == 0) {
        // Overflow the MAIN stack, to prove the MSPLIM guard fires.
        // It has to happen in handler mode, because that is the only context
        // that runs on MSP - so borrow the same spare IRQ as `crash irq` and
        // recurse in its handler instead of faulting.
        //
        // A clean result is a HARDFAULT whose UsageFault status has STKOF set.
        // A LOCKUP instead means the reserve kept below MSPLIM is too small
        // for the fault to stack its own exception frame, which is the failure
        // this command exists to catch.
        sTampRecurse = true;
        NVIC_EnableIRQ(TAMP_IRQn);
        NVIC_SetPendingIRQ(TAMP_IRQn);
        __DSB();
        __ISB();
        // The IRQ preempts immediately; control does not return here.
        for (;;) {}
    } else if (strcmp(type, "irq") == 0) {
        // Crash inside an ISR: pend a spare IRQ (TAMP) whose handler faults, so
        // the dump captures a handler-mode (MSP-stacked) crash.
        NVIC_EnableIRQ(TAMP_IRQn);
        NVIC_SetPendingIRQ(TAMP_IRQn);
        __DSB();
        __ISB();
        // The IRQ preempts immediately; control does not return here.
        for (;;) {}
    } else {
        crashPrintUsage();
    }
}
