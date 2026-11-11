/**
 ******************************************************************************
 * @file    ProdTestBuzzer.hpp
 * @brief   Production test for the Buzzer.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "CLI/CLIParser.h"

namespace ProdTest {

class Buzzer : public Interface::IProdTest {
public:
    Buzzer(const std::string& id);
    ~Buzzer() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    void startBuzzer(StringLogger& slog, int argc, const char** argv);
    void playTone(int argc, const char** argv);
    void sweep(int argc, const char** argv);
    void playNotes(int argc, const char** argv);
    void playAlert(int argc, const char** argv);
    void checkCommand(StringLogger& slog, CLIInput_t* input);
};

}   // namespace ProdTest
