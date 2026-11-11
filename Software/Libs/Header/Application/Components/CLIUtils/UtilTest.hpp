/**
 *******************************************************************************
 * @file    UtilTest.hpp
 * @brief   CLI utility that allows you to test the PMIC
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#pragma once

#include "Porting.h"
#include "CLI/UtilBase.hpp"
#include "CLI/ICommand.h"

namespace Components {

namespace CliUtils {

class UtilTest : public UtilBase
{
public:
    static UtilTest &getInstance();

    UtilTest(UtilTest const&)       = delete;
    void operator=(UtilTest const&) = delete;

    UtilTest(UtilTest&&)            = delete;
    UtilTest& operator=(UtilTest&&) = delete;

    ICLICommand_t *getCommands() override;

private:
    ICLICommand_t mCommands;

    explicit UtilTest();
    ~UtilTest() override = default;

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
