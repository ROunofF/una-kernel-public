/**
 ******************************************************************************
 * @file    ProdTestLCD.hpp
 * @brief   Production test for the LCD.
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "ProdTest/IProdTest.hpp"
#include "Backlight.hpp"

namespace ProdTest {

class LcdBackLight : public Interface::IProdTest {
public:
    LcdBackLight(const std::string& id);
    ~LcdBackLight() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    void cmdOn(StringLogger& slog);
    void cmdOff(StringLogger& slog);
    void checkCommand(StringLogger& slog, CLIInput_t* input);

    Backlight* mBacklight;
};

}   // namespace ProdTest
