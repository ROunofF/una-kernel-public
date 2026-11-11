#ifndef ERRORVIEW_HPP
#define ERRORVIEW_HPP

#include <gui_generated/error_screen/ErrorViewBase.hpp>
#include <gui/error_screen/ErrorPresenter.hpp>

class ErrorView : public ErrorViewBase
{
public:
    ErrorView();
    virtual ~ErrorView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
    void setError(SystemError e);
protected:
    uint16_t mRepeatPeriod = 0;
    bool     mMuted = false;   ///< R1 silenced the buzzer/vibro for this screen.

    /// Vertical centre of the name field as the designer laid it out, captured
    /// before the field is resized to its text. Taken from the widget itself so
    /// that moving it in the designer does not silently misplace the text.
    int16_t  mNameCentreY = 0;

    virtual void handleKeyEvent(uint8_t key) override;
    virtual void handleTickEvent() override;
};

#endif // ERRORVIEW_HPP
