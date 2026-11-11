/**
 ******************************************************************************
 * @file    ProdTestController.hpp
 * @brief   Controller that runs a production test and mirrors its string
 *          history to the console.
 *
 * @details The controller constructs a StringLogger, passes it to the provided
 *          Interface::IProdTest via run(StringLogger&), prints a one-line
 *          summary ("ID=TEST_ID_<name>, RESULT=PASS|FAIL"), and then prints
 *          all captured messages as a comma-separated list on the same line.
 *          Trailing newlines in stored messages are stripped to keep output
 *          compact and single-line.
 ******************************************************************************
 */

#ifndef __PROD_TEST_CONTROLLER_HPP
#define __PROD_TEST_CONTROLLER_HPP

#include "ProdTest/IProdTest.hpp"

#include "CLI/CLIConsole.h"
#include "CLI/CLIParser.h"
#include <CLI/CLIInput.h>

class ProdTestController {
public:
    explicit ProdTestController(Interface::IProdTest& test,
                                CLIParser_t&          p,
                                CLIInput_t*           input);

    ~ProdTestController() = default;

    void execute();

private:
    Interface::IProdTest& mTest;
    CLIParser_t&          mParser;
    CLIInput_t*           mInput;
};

#endif  // __STRING_LOGGER_HPP
