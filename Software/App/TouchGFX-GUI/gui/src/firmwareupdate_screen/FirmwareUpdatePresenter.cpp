#include <gui/firmwareupdate_screen/FirmwareUpdateView.hpp>
#include <gui/firmwareupdate_screen/FirmwareUpdatePresenter.hpp>

FirmwareUpdatePresenter::FirmwareUpdatePresenter(FirmwareUpdateView& v)
    : view(v)
{

}

void FirmwareUpdatePresenter::activate()
{
    view.setUpdatePct(model->firmwareUpdateGetPct());
}

void FirmwareUpdatePresenter::deactivate()
{

}

void FirmwareUpdatePresenter::onFirmwareUpdPct(uint8_t pct)
{
    view.setUpdatePct(pct);
}

void FirmwareUpdatePresenter::onFirmwareUpdEnd()
{
    model->application().gotoHomeScreenNoTransition();
}