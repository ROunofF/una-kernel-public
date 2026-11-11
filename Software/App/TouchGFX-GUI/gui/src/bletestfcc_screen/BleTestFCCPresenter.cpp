#include <gui/bletestfcc_screen/BleTestFCCView.hpp>
#include <gui/bletestfcc_screen/BleTestFCCPresenter.hpp>

BleTestFCCPresenter::BleTestFCCPresenter(BleTestFCCView& v)
    : view(v)
{

}

void BleTestFCCPresenter::activate()
{
    model->resetIdleTimer();
    view.setPositionId(0);
}

void BleTestFCCPresenter::deactivate()
{
    model->setPosMenuBleFCC(view.getPositionId());
}
