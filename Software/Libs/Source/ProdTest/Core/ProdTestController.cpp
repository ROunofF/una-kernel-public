/**
 ******************************************************************************
 * @file    ProdTestController.cpp
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

#include "ProdTest/Core/ProdTestController.hpp"

#include "CLI/CLIConsole.h"

ProdTestController::ProdTestController(Interface::IProdTest& test,
                                       CLIParser_t&          p,
                                       CLIInput_t*           input)
    : mTest(test)
    , mParser(p)
    , mInput(input)
{
}

void ProdTestController::execute()
{
    StringLogger slog;

    bool pass = mTest.run(slog, mParser, mInput);

    CLIConsole_print("ID=%s,RESULT=%s", mTest.getName().c_str(), pass ? "PASS" : "FAIL");

    const auto& lines = slog.lines();
    if (!lines.empty()) {
        for (const auto& s : lines) {
            // remove the final '\n' in each string, if any
            std::string view = s;
            if (!view.empty() && view.back() == '\n') {
                view.pop_back();
            }

            CLIConsole_print(",%s", view.c_str());
        }
    }

    CLIConsole_print("\n");
}
