#ifndef PAIRCODEVIEW_HPP
#define PAIRCODEVIEW_HPP

#include <gui_generated/paircode_screen/PairCodeViewBase.hpp>
#include <gui/paircode_screen/PairCodePresenter.hpp>

class PairCodeView : public PairCodeViewBase
{
public:
    PairCodeView();
    virtual ~PairCodeView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setCode(uint32_t pinCode);

protected:
    uint16_t mCounter = Gui::Config::kPairCodeScreenTimeout;
    virtual void handleTickEvent() override;
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // PAIRCODEVIEW_HPP
