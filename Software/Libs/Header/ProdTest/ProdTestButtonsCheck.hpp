/**
 ******************************************************************************
 * @file    ProdTestButtons.hpp
 * @brief   Production test for the Buttons.
 ******************************************************************************
 *
 ******************************************************************************
 */


#include "ProdTest/IProdTest.hpp"
#include "ProdTest/LCD/TestLCD.hpp"
#include "SwTimer.hpp"

namespace ProdTest {

class ButtonsCheck : public Interface::IProdTest {
public:
    ButtonsCheck(const std::string& id);

    ~ButtonsCheck() override = default;

    bool run(StringLogger& slog, CLIParser_t& p, CLIInput_t* input) override;

private:
    void fixButtonsState();
    void refreshButtonsState(ProdTest::TestLCD& test);

    void setButtonState(bool state, uint16_t pos);
    void updateButtonsState();

    void cmdSwCheck() const;
    void checkCommand(StringLogger& slog, CLIInput_t* input);

    uint16_t        mButtonsHistory;
    uint16_t        mButtonsState;
    uint16_t        mPrevButtonsState;
    uint16_t        mSW2Counter;
    Driver::SwTimer mUpdateButtonsStateTimer;
    Driver::SwTimer mTimeout;
};

}   // namespace ProdTest
