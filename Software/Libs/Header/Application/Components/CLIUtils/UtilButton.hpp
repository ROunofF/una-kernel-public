/**
 *******************************************************************************
 * @file    UtilButton.hpp
 * @brief   CLI utility for buttons testing
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#pragma once

#include <Porting.h>
#include "CLI/UtilBase.hpp"
#include "CLI/ICommand.h"

namespace Components::CliUtils {

    class UtilButton : public UtilBase
    {
    public:
        static UtilButton& getInstance();

        UtilButton(UtilButton const&)     = delete;
        void operator=(UtilButton const&) = delete;

        UtilButton(UtilButton&&)            = delete;
        UtilButton& operator=(UtilButton&&) = delete;

        ICLICommand_t* getCommands() override;

    private:
        ICLICommand_t mCommands;

        explicit UtilButton();
        ~UtilButton() override = default;

        static const char* getName(ICLICommand_t* cmd);
        static const char* getDesc(ICLICommand_t* cmd);
        static const char* getHelp(ICLICommand_t* cmd);
        static void        execute(ICLICommand_t* cmd,
                                   CLIInput_t*    input,
                                   int            argc,
                                   const char**   argv);
    };

} // namespace Components::CliUtils

