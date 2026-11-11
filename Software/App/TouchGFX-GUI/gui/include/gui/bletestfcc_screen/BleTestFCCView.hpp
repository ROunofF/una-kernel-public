#ifndef BLETESTFCCVIEW_HPP
#define BLETESTFCCVIEW_HPP

#include <gui_generated/bletestfcc_screen/BleTestFCCViewBase.hpp>
#include <gui/bletestfcc_screen/BleTestFCCPresenter.hpp>

class BleTestFCCView : public BleTestFCCViewBase
{
public:
    BleTestFCCView();
    virtual ~BleTestFCCView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();
protected:

    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // BLETESTFCCVIEW_HPP
