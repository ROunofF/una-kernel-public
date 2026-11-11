/**
 *******************************************************************************
 * @file    UtilFormat.hpp
 * @brief   CLI utility that allows you to format the file system
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

class UtilFormat : public UtilBase
{
public:
    static UtilFormat &getInstance();

    UtilFormat(UtilFormat const&)     = delete;
    void operator=(UtilFormat const&) = delete;

    UtilFormat(UtilFormat&&)            = delete;
    UtilFormat& operator=(UtilFormat&&) = delete;

    ICLICommand_t *getCommands() override;

private:
    ICLICommand_t mCommands;

    explicit UtilFormat();
    ~UtilFormat() override = default;

    static bool waitConfirm(void);

    static const char* getName(ICLICommand_t* cmd);
    static const char* getDesc(ICLICommand_t* cmd);
    static const char* getHelp(ICLICommand_t* cmd);
    static void        execute(ICLICommand_t* cmd,
                               CLIInput_t*    input,
                               int            argc,
                               const char**   argv);
};

}
}
