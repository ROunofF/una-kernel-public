/**
 *******************************************************************************
 * @file    UtilCrash.hpp
 * @brief   CLI utility that crashes the device to test the CrashDump component
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#pragma once

#include <Porting.h>
#include "CLI/UtilBase.hpp"
#include "CLI/ICommand.h"

namespace Components {

namespace CliUtils {

class UtilCrash : public UtilBase
{
public:
    static UtilCrash &getInstance();

    UtilCrash(UtilCrash const&)      = delete;
    void operator=(UtilCrash const&) = delete;

    UtilCrash(UtilCrash&&)            = delete;
    UtilCrash& operator=(UtilCrash&&) = delete;

    ICLICommand_t *getCommands() override;

private:
    ICLICommand_t mCommands;

    explicit UtilCrash();
    ~UtilCrash() override = default;

    static const char *getName(ICLICommand_t *pCmd);
    static const char *getDesc(ICLICommand_t *pCmd);
    static const char *getHelp(ICLICommand_t *pCmd);
    static void        execute(ICLICommand_t *pCmd,
                               CLIInput_t    *pInput,
                               int            argc,
                               const char   **pArgv);
};

}
}
