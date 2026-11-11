/**
 *******************************************************************************
 * @file    UtilTime.hpp
 * @brief   CLI utility for time management
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#pragma once

#include "Porting.h"
#include "CLI/UtilBase.hpp"
#include "CLI/ICommand.h"

namespace Components::CliUtils {

    class UtilTime : public UtilBase
    {
    public:
        static UtilTime &getInstance();

        UtilTime(const UtilTime&)       = delete;
        void operator=(const UtilTime&) = delete;

        UtilTime(UtilTime&&)            = delete;
        UtilTime& operator=(UtilTime&&) = delete;

        ICLICommand_t* getCommands() override;

    private:
        ICLICommand_t mCommands;

        explicit UtilTime();
        ~UtilTime() override = default;

        static const char* getName(ICLICommand_t* cmd);
        static const char* getDesc(ICLICommand_t* cmd);
        static const char* getHelp(ICLICommand_t* cmd);
        static void        execute(ICLICommand_t* cmd,
                                   CLIInput_t*    input,
                                   int            argc,
                                   const char**   argv);

        static bool isdigit(char s);
        static bool checkDateFormat(const char* dateString);
        static bool checkDateItems(int y, int m, int d);
        static bool checkTimeFormat(const char* timeString);
        static bool checkTimeItems(int hh, int mm, int ss);
        static bool setDateTime(const char* d, const char* t);
    };

}
