#ifndef FIRMWAREUPDATEVIEW_HPP
#define FIRMWAREUPDATEVIEW_HPP

#include <gui_generated/firmwareupdate_screen/FirmwareUpdateViewBase.hpp>
#include <gui/firmwareupdate_screen/FirmwareUpdatePresenter.hpp>

class FirmwareUpdateView : public FirmwareUpdateViewBase
{
public:
    FirmwareUpdateView();
    virtual ~FirmwareUpdateView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setUpdatePct(uint8_t pct);
protected:
};

#endif // FIRMWAREUPDATEVIEW_HPP
