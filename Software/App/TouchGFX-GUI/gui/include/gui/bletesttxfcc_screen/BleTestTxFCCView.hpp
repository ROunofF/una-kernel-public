#ifndef BLETESTTXFCCVIEW_HPP
#define BLETESTTXFCCVIEW_HPP

#include <gui_generated/bletesttxfcc_screen/BleTestTxFCCViewBase.hpp>
#include <gui/bletesttxfcc_screen/BleTestTxFCCPresenter.hpp>

class BleTestTxFCCView : public BleTestTxFCCViewBase
{
public:
    BleTestTxFCCView();
    virtual ~BleTestTxFCCView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setChannel(const BleChannelsFCC::Info& ch);
protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // BLETESTTXFCCVIEW_HPP
