#include <gui/settingsgpsassist_screen/SettingsGpsAssistView.hpp>
#include <gui/settingsgpsassist_screen/SettingsGpsAssistPresenter.hpp>

SettingsGpsAssistPresenter::SettingsGpsAssistPresenter(SettingsGpsAssistView& v)
    : view(v)
{

}

void SettingsGpsAssistPresenter::activate()
{
    // Reset idle timer
    model->resetIdleTimer();

    onGpsEpoSyncState(model->getGpsEpoSyncState());
}

void SettingsGpsAssistPresenter::deactivate()
{

}

void SettingsGpsAssistPresenter::onGpsEpoTime(uint32_t h)
{
    if (!model->getGpsEpoSyncState()) {
        view.setEpoTimeH(h);
    }
}

void SettingsGpsAssistPresenter::onGpsEpoSyncState(bool state)
{
    if (state) {
        view.setEpoSync();
    } else {
        model->requestEpoUpdate();
        view.setEpoTimeH(model->getEpoTimeH());
    }
}