#include <gui/bletesttxfcc_screen/BleTestTxFCCView.hpp>
#include <gui/bletesttxfcc_screen/BleTestTxFCCPresenter.hpp>

BleTestTxFCCPresenter::BleTestTxFCCPresenter(BleTestTxFCCView& v)
    : view(v)
{

}

void BleTestTxFCCPresenter::activate()
{
    model->resetIdleTimer();
    model->bleStartFCC(BleChannelsFCC::list.at(model->getPosMenuBleFCC()));
    view.setChannel(BleChannelsFCC::list.at(model->getPosMenuBleFCC()));
}

void BleTestTxFCCPresenter::deactivate()
{
    model->bleStopFCC();
}
