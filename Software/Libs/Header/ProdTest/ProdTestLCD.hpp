/**
 ******************************************************************************
 * @file    ProdTestLCD.hpp
 * @brief   Production test for the LCD.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "ProdTest/LCD/TestLCD.hpp"

namespace ProdTest {

class Lcd : public Interface::IProdTest {
public:
    Lcd(const std::string& id);
    ~Lcd() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    void setColor(StringLogger& slog, ProdTest::TestLCD& test, int argc, const char** argv);
    void checkCommand(StringLogger& slog, ProdTest::TestLCD& test, CLIInput_t* input);
};

}   // namespace ProdTest
