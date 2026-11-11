#ifndef PAIRINGINFOVIEW_HPP
#define PAIRINGINFOVIEW_HPP

#include <gui_generated/pairinginfo_screen/PairingInfoViewBase.hpp>
#include <gui/pairinginfo_screen/PairingInfoPresenter.hpp>

class PairingInfoView : public PairingInfoViewBase
{
public:
    PairingInfoView();
    virtual ~PairingInfoView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // PAIRINGINFOVIEW_HPP
