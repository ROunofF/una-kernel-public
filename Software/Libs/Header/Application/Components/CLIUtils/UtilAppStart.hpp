/**
 *******************************************************************************
 * @file    UtilAppStart.hpp
 * @brief   CLI utility that starts/stops a user application by PID, posting
 *          the same G2B events the launcher GUI posts. Development aid for
 *          driving app lifecycle when the GUI is unavailable (e.g. bench
 *          testing over USB, where the charging screen owns the display).
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

class UtilAppStart : public UtilBase
{
public:
    static UtilAppStart &getInstance();

    UtilAppStart(UtilAppStart const&)      = delete;
    void operator=(UtilAppStart const&)    = delete;

    UtilAppStart(UtilAppStart&&)            = delete;
    UtilAppStart& operator=(UtilAppStart&&) = delete;

    ICLICommand_t *getCommands() override;

private:
    ICLICommand_t mCommands;

    explicit UtilAppStart();
    ~UtilAppStart() override = default;

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
