#ifndef PAIRINGCANCELVIEW_HPP
#define PAIRINGCANCELVIEW_HPP

#include <gui_generated/pairingcancel_screen/PairingCancelViewBase.hpp>
#include <gui/pairingcancel_screen/PairingCancelPresenter.hpp>

class PairingCancelView : public PairingCancelViewBase
{
public:
    PairingCancelView();
    virtual ~PairingCancelView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // PAIRINGCANCELVIEW_HPP
