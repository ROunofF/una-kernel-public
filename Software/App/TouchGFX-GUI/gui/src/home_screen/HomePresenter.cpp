#include <gui/home_screen/HomeView.hpp>
#include <gui/home_screen/HomePresenter.hpp>

HomePresenter::HomePresenter(HomeView& v)
    : view(v)
{

}

void HomePresenter::activate()
{
    // Where the face's frames can be read from. Announced once at boot, so it
    // only has to be handed to the view.
    view.setAppSurface(model->getAppSurface());

    // Whose frames to expect, before the screen is drawn for the first time.
    // Asking the backend and waiting for the answer would cost a frame of the
    // built-in face on the way in, every time.
    view.setWatchFace(model->getWatchFacePid());

    // What decides whether a watch face app is drawing. Said here rather than
    // inferred from an app starting or stopping: the question is which screen
    // is up, and this is the screen that knows.
    model->setHomeScreenShown(true);

    // Nothing of this screen's own moves: the built-in face changes once a
    // minute, on an event that asks for a frame when it happens. Whether a
    // face app draws over it at its own pace is not this screen's to say.
    //
    // Last of the two, as the two are handled in the order they are sent: the
    // steady beat carries on until the face has been asked to draw, and stops
    // only once something else is there to ask for frames. Reversed on the way
    // out, for the same reason.
    model->setScreenFrameRate(0);

    // Reset idle timer
    model->resetIdleTimer();

    // Reset nested menu position
    model->setPosMenuTop(Gui::Config::Menu::Top::ID_DEFAULT);
    model->setPosMenuTracking(0);
    model->phoneNotificationSetUidForDetails(0);
    model->setPosMenuGlances(0);

    // Applied before the first draw, so the clock lines are composed in the
    // configured format straight away.
    view.setClockSettings(model->getLocalSettings().clock);

    uint8_t month;
    uint8_t day;
    uint8_t wday;
    model->getDate(month, day, wday);
    view.setDate(month, day, wday);

    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    model->getTime(hour, minute, second);
    view.setTime(hour, minute, second);

    view.setBatteryLevel(model->batteryGetLevel());
    view.setMuteAlerts(model->getLocalSettings().alertMutedFlag);

    auto sett = model->getCommonSettings();
    view.setDailyHealth(model->getDailySteps(), sett.dailyGoals.steps,
        model->getDailyFloors(), sett.dailyGoals.floors, model->getDailyActivityTime(), sett.dailyGoals.activityMinutes);

    // A widget pushed while the home face was away takes over the bottom bar now.
    const auto &w = model->homeWidget();
    if (w.active) {
        view.setHomeWidget(w);
    }
}

void HomePresenter::deactivate()
{
    model->setScreenFrameRate(App::kFrameRate);
    model->setHomeScreenShown(false);
}

void HomePresenter::onWatchFaceRunning(uint32_t guiPid)
{
    view.setWatchFace(guiPid);
}

void HomePresenter::onBatteryLevel(uint8_t lvl)
{
    view.setBatteryLevel(lvl);
}

void HomePresenter::onCommonSettings(const WatchSettings &sett)
{
    view.setDailyHealth(model->getDailySteps(), sett.dailyGoals.steps,
        model->getDailyFloors(), sett.dailyGoals.floors, model->getDailyActivityTime(), sett.dailyGoals.activityMinutes);
}

void HomePresenter::onLocalSettings(const WatchSettingsLocal &sett)
{
    view.setClockSettings(sett.clock);
    view.setMuteAlerts(sett.alertMutedFlag);
}

void HomePresenter::onTime(uint8_t h, uint8_t m, uint8_t s)
{
    view.setTime(h, m, s);
}

void HomePresenter::onDate(uint8_t m, uint8_t d, uint8_t wd)
{
    view.setDate(m, d, wd);
}

void HomePresenter::onDailyHealth(uint32_t steps, uint32_t floors, uint32_t activityMinutes)
{
    auto sett = model->getCommonSettings();
    view.setDailyHealth(steps, sett.dailyGoals.steps,
        floors, sett.dailyGoals.floors, activityMinutes, sett.dailyGoals.activityMinutes);
}

void HomePresenter::onHomeWidget(const HomeWidgetData &widget)
{
    view.setHomeWidget(widget);

    if (!widget.active) {
        // Widget cleared: restore the activity progress it was covering.
        auto sett = model->getCommonSettings();
        view.setDailyHealth(model->getDailySteps(), sett.dailyGoals.steps,
            model->getDailyFloors(), sett.dailyGoals.floors,
            model->getDailyActivityTime(), sett.dailyGoals.activityMinutes);
    }
}
