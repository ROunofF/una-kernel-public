/**
 *******************************************************************************
 * @file    UtilTime.cpp
 * @brief   CLI utility for time management
 *
 *******************************************************************************
 *
 *******************************************************************************
 */

#include "Components/CLIUtils/UtilTime.hpp"
#include "CLI/CLIParser.h"
#include "CLI/CLIConsole.h"
#include "Components/Misc/utiltime.hpp"
#include "Rtc.hpp"

#include <string.h>

/**
 * @brief Construct a new Components::CliUtils::UtilTime object
 * 
 */
Components::CliUtils::UtilTime::UtilTime() :
        UtilBase("time"),
        mCommands{
            .execute = UtilTime::execute,
            .getName = UtilTime::getName,
            .getDesc = UtilTime::getDesc,
            .getHelp = UtilTime::getHelp,
        }
{

}

/**
 * @brief  Returns the reference to a single instance of the Test CLI
 *         utility
 * 
 * @return A reference to the utility object
 */
Components::CliUtils::UtilTime& Components::CliUtils::UtilTime::getInstance()
{
    static UtilTime instance;

    return instance;
}

/**
 * @brief  Returns a pointer to the structure with pointers to static command
 *         handlers
 * 
 * @return A pointer to the ICLICommand_t
 */
ICLICommand_t* Components::CliUtils::UtilTime::getCommands()
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
const char *Components::CliUtils::UtilTime::getName(ICLICommand_t* cmd)
{
    return UtilTime::getInstance().getUtilName();
}

/**
 * @brief  Returns a brief utility description
 * 
 * @param  pCmd: A pointer to the command handler functions
 * 
 * @return Utility description
 */
const char *Components::CliUtils::UtilTime::getDesc(ICLICommand_t* cmd)
{
    return "time tools";
}

/**
 * @brief  Provides a help on how to use the utility
 * 
 * @param  pCmd: A pointer to the command handler functions
 * 
 * @return Empty ""
 */
const char *Components::CliUtils::UtilTime::getHelp(ICLICommand_t* cmd)
{
    static const char* help = "The list of the internal commands:\n" \
                              "\n" \
                              " - " CLI_ATTR_CMD_HELP "time" CLI_ATTR_DEF "\n" \
                              "   shows current time\n" \
                              "\n" \
                              "\n" \
                              " - " CLI_ATTR_CMD_HELP "time -r" CLI_ATTR_DEF "\n" \
                              "   resets time\n" \
                              "\n" \
                              " - " CLI_ATTR_CMD_HELP "time yyyy/mm/dd hh:mm:ss" CLI_ATTR_DEF "\n" \
                              "   sets new time\n" \
                              "\n" \
                              "\n" \
                              " - " CLI_ATTR_CMD_HELP "help" CLI_ATTR_DEF "\n" \
                              "   shows this help\n";

    return help;
}

/**
 * @brief  Executes the request to the utility
 * 
 * @param  cmd:   Pointer to the command handler functions
 * @param  input: Pointer to the CLI input
 * @param  argc:  Count of the arguments
 * @param  argv:  Pointer to the arguments
 */
void Components::CliUtils::UtilTime::execute(ICLICommand_t* cmd,
                                             CLIInput_t*    input,
                                             int            argc,
                                             const char**   argv)
{
    (void) cmd;
    (void) input;

    CLIParser_t p;
    CLIParser_init(&p, argc, argv);

    Driver::Rtc& rtc = Driver::Rtc::GetInstance();

    if (CLIParser_check(&p, "-r")) {
        rtc.setTimeZone(0);
        struct tm dateTime{};
        rtc.set(dateTime);
    }

    if (argc == 3) {
        if (!setDateTime(argv[1], argv[2])) {
            return;
        }
    }

//    if (!rtc.isValide()) {
//        CLIConsole_print("time is reset\n");
//        return;
//    }

    CLIConsole_print("%s\n", Misc::Time::time2str(time(NULL)).c_str());
}

bool Components::CliUtils::UtilTime::isdigit(char s)
{
    return ((s >= '0') && (s <= '9'));
}

bool Components::CliUtils::UtilTime::checkDateFormat(const char* dateString)
{
    if (strlen(dateString) != 10) {
        return false;
    }

    if (Components::CliUtils::UtilTime::isdigit(dateString[0]) == false) { return false; }
    if (Components::CliUtils::UtilTime::isdigit(dateString[1]) == false) { return false; }
    if (Components::CliUtils::UtilTime::isdigit(dateString[2]) == false) { return false; }
    if (Components::CliUtils::UtilTime::isdigit(dateString[3]) == false) { return false; }
    if (dateString[4] != '/')            { return false; }
    if (Components::CliUtils::UtilTime::isdigit(dateString[5]) == false) { return false; }
    if (Components::CliUtils::UtilTime::isdigit(dateString[6]) == false) { return false; }
    if (dateString[7] != '/')            { return false; }
    if (Components::CliUtils::UtilTime::isdigit(dateString[8]) == false) { return false; }
    if (Components::CliUtils::UtilTime::isdigit(dateString[9]) == false) { return false; }

    return true;
}

bool Components::CliUtils::UtilTime::checkDateItems(int y, int m, int d)
{
    if (y < 2020) { return false; }
    if (y > 2030) { return false; }
    if (m < 1)    { return false; }
    if (m > 12)   { return false; }
    if (d < 1)    { return false; }
    if (d > 31)   { return false; }

    return true;
}

bool Components::CliUtils::UtilTime::checkTimeFormat(const char* timeString)
{
    if (strlen(timeString) != 8) {
        return false;
    }

    if (Components::CliUtils::UtilTime::isdigit(timeString[0]) == false) { return false; }
    if (Components::CliUtils::UtilTime::isdigit(timeString[1]) == false) { return false; }
    if (timeString[2] != ':')            { return false; }
    if (Components::CliUtils::UtilTime::isdigit(timeString[3]) == false) { return false; }
    if (Components::CliUtils::UtilTime::isdigit(timeString[4]) == false) { return false; }
    if (timeString[5] != ':')            { return false; }
    if (Components::CliUtils::UtilTime::isdigit(timeString[6]) == false) { return false; }
    if (Components::CliUtils::UtilTime::isdigit(timeString[7]) == false) { return false; }

    return true;
}

bool Components::CliUtils::UtilTime::checkTimeItems(int hh, int mm, int ss)
{
    if (hh < 0)  { return false; }
    if (hh > 23) { return false; }
    if (mm < 0)  { return false; }
    if (mm > 59) { return false; }
    if (ss < 0)  { return false; }
    if (ss > 59) { return false; }

    return true;
}

bool Components::CliUtils::UtilTime::setDateTime(const char* d, const char* t)
{
    if (Components::CliUtils::UtilTime::checkDateFormat(d) == false) {
        CLIConsole_print("date format is incorrect");
        return false;
    }

    if (Components::CliUtils::UtilTime::checkTimeFormat(t) == false) {
        CLIConsole_print("time format is incorrect\n");
        return false;
    }

    int y;
    int m;
    int day;
    sscanf(d, "%04d/%02d/%02d", &y, &m, &day);

    if (Components::CliUtils::UtilTime::checkDateItems(y, m, day) == false) {
        CLIConsole_print("date values is incorrect\n");
        return false;
    }

    int hh;
    int mm;
    int ss;
    sscanf(t, "%02d:%02d:%02d", &hh, &mm, &ss);

    if (checkTimeItems(hh, mm, ss) == false) {
        CLIConsole_print("time values is incorrect\n");
        return false;
    }

    Driver::Rtc& rtc = Driver::Rtc::GetInstance();

    struct tm dt{};

    dt.tm_year = y - 1900;
    dt.tm_mon  = m - 1;
    dt.tm_mday = day;
    dt.tm_hour = hh;
    dt.tm_min  = mm;
    dt.tm_sec  = ss;

    rtc.set(dt);

    return true;
}
