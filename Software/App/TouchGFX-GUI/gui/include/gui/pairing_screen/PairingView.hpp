#ifndef PAIRINGVIEW_HPP
#define PAIRINGVIEW_HPP

#include <gui_generated/pairing_screen/PairingViewBase.hpp>
#include <gui/pairing_screen/PairingPresenter.hpp>

class PairingView : public PairingViewBase
{
public:
    PairingView();
    virtual ~PairingView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setDeviceID(const char *idStr);

protected:
    uint16_t mCounter = Gui::Config::kPairCodeScreenTimeout;
    virtual void handleTickEvent() override;
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // PAIRINGVIEW_HPP
