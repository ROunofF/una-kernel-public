/**
 *******************************************************************************
 * @file    UtilReset.hpp
 * @brief   CLI utility that allows you to reset the device
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

class UtilReset : public UtilBase
{
public:
    static UtilReset &getInstance();

    UtilReset(UtilReset const&)      = delete;
    void operator=(UtilReset const&) = delete;

    UtilReset(UtilReset&&)            = delete;
    UtilReset& operator=(UtilReset&&) = delete;

    ICLICommand_t *getCommands() override;

private:
    ICLICommand_t mCommands;

    explicit UtilReset();
    ~UtilReset() override = default;

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
