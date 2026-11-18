/**
 *******************************************************************************
 * @file    UtilUsb.hpp
 * @brief   CLI utility for the USB drive: open or close it with the cable in,
 *          and whether it reopens after a reset.
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

class UtilUsb : public UtilBase
{
public:
    static UtilUsb &getInstance();

    UtilUsb(UtilUsb const&)            = delete;
    void operator=(UtilUsb const&)     = delete;

    UtilUsb(UtilUsb&&)                 = delete;
    UtilUsb& operator=(UtilUsb&&)      = delete;

    ICLICommand_t *getCommands() override;

private:
    ICLICommand_t mCommands;

    explicit UtilUsb();
    ~UtilUsb() override = default;

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
