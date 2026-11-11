/**
 *******************************************************************************
 * @file    UtilBase.hpp
 * @brief   This file contains the CLI console Base utility.
 *******************************************************************************
 *
 *******************************************************************************
 */

#pragma once

#include <Porting.h>
#include "CLI/ICommand.h"
#include <string>
#include <vector>

#include "TimerSysMs.h"

namespace Components::CliUtils
{

    class UtilBase
    {
    public:
        explicit UtilBase(const char* name);
        virtual ~UtilBase() = default;

        virtual ICLICommand_t* getCommands() = 0;

    protected:
        const char*        getUtilName();
        const std::string& getUtilNameStr();

    private:
        std::string mUtilName;
    };

} // namespace Components::CliUtils
