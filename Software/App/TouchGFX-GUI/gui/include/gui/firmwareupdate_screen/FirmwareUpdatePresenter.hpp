#ifndef FIRMWAREUPDATEPRESENTER_HPP
#define FIRMWAREUPDATEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class FirmwareUpdateView;

class FirmwareUpdatePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    FirmwareUpdatePresenter(FirmwareUpdateView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~FirmwareUpdatePresenter() {}

    virtual void onFirmwareUpdPct(uint8_t pct) override;
    virtual void onFirmwareUpdEnd() override;

private:
    FirmwareUpdatePresenter();

    FirmwareUpdateView& view;
};

#endif // FIRMWAREUPDATEPRESENTER_HPP
