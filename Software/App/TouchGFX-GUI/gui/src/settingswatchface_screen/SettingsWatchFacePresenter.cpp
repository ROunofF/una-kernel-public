#include <gui/settingswatchface_screen/SettingsWatchFaceView.hpp>
#include <gui/settingswatchface_screen/SettingsWatchFacePresenter.hpp>

#define LOG_MODULE_PRX      "GUI.WatchFace"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "gui/common/GuiLogger.hpp"

SettingsWatchFacePresenter::SettingsWatchFacePresenter(SettingsWatchFaceView& v)
    : view(v)
{

}

void SettingsWatchFacePresenter::activate()
{
    model->resetIdleTimer();

    view.setAppSurface(model->getAppSurface());

    // Whose frames to expect, before the first draw -- the face on the surface
    // is the one the home screen was just showing.
    view.setWatchFace(model->getWatchFacePid());

    // The stored face is already loaded -- it was drawing the home screen a
    // moment ago -- so it can be shown before anything is known about the rest
    // of the list, which takes a few ticks to arrive.
    mSelectedId = model->getWatchFace();
    mFaceCount = 0;
    mIndex = 0;
    model->watchFacePreview(mSelectedId, 0, 0);

    // Where this face sits among the others is not known yet, so the indicator
    // says nothing rather than pointing at the first position.
    view.setCursor(0, 0);

    // Ids alone: what this screen shows of a face is the face itself.
    model->appRequestIds(App::Type::kClockface);

    // Feeds the built-in face, which is what position 0 of the list looks
    // like. An app face covers it whole, so this is wasted then but harmless.
    updateFace();
}

void SettingsWatchFacePresenter::deactivate()
{
    // Whatever the screen is leaving for -- applied, cancelled, or a
    // notification taking over -- only the stored face stays loaded. On a
    // cancel that undoes the preview by itself, since the store still holds
    // the face the user came in with.
    model->watchFacePreviewEnd(model->getWatchFace());
}

void SettingsWatchFacePresenter::onWatchFaceRunning(uint32_t guiPid)
{
    view.setWatchFace(guiPid);
}

void SettingsWatchFacePresenter::onAppIds(App::Type type,
                                          const std::shared_ptr<std::vector<uint64_t>> &ids)
{
    if (type != App::Type::kClockface) {
        return;
    }

    mFaceIds[0] = 0;   // the built-in face
    mFaceCount = 1;

    if (ids) {
        for (const uint64_t id : *ids) {
            if (mFaceCount >= kMaxFaces) {
                LOG_WARNING("%u faces installed, picker shows the first %u\n",
                        static_cast<unsigned>(ids->size()),
                        static_cast<unsigned>(kMaxFaces - 1));
                break;
            }
            mFaceIds[mFaceCount++] = id;
        }
    }

    // Place the cursor on the face that is already on screen. A stored face
    // that is no longer installed leaves it at the built-in one, which is what
    // the watch is showing in that case anyway.
    mIndex = 0;
    for (uint8_t i = 0; i < mFaceCount; ++i) {
        if (mFaceIds[i] == mSelectedId) {
            mIndex = i;
            break;
        }
    }
    mSelectedId = mFaceIds[mIndex];

    view.setCursor(mFaceCount, mIndex);

    showSelected();
}

void SettingsWatchFacePresenter::selectPrev()
{
    if (mFaceCount < 2) {
        return;
    }

    mIndex = (mIndex == 0) ? (mFaceCount - 1) : (mIndex - 1);
    mSelectedId = mFaceIds[mIndex];
    view.stepCursor(-1);
    showSelected();
}

void SettingsWatchFacePresenter::selectNext()
{
    if (mFaceCount < 2) {
        return;
    }

    mIndex = (mIndex + 1 < mFaceCount) ? (mIndex + 1) : 0;
    mSelectedId = mFaceIds[mIndex];
    view.stepCursor(+1);
    showSelected();
}

void SettingsWatchFacePresenter::apply()
{
    model->saveWatchFace(mSelectedId);
}

void SettingsWatchFacePresenter::showSelected()
{
    model->resetIdleTimer();

    if (mFaceCount == 0) {
        // Before the list has arrived there is no neighbour to name, and the
        // arithmetic below would run off the end of the array looking for one.
        model->watchFacePreview(mSelectedId, 0, 0);
        return;
    }

    const uint8_t prev = (mIndex == 0) ? (mFaceCount - 1) : (mIndex - 1);
    const uint8_t next = (mIndex + 1 < mFaceCount) ? (mIndex + 1) : 0;

    model->watchFacePreview(mSelectedId, mFaceIds[prev], mFaceIds[next]);
}

void SettingsWatchFacePresenter::updateFace()
{
    view.setClockSettings(model->getLocalSettings().clock);
    view.setMuteAlerts(model->getLocalSettings().alertMutedFlag);
    view.setBatteryLevel(model->batteryGetLevel());

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

    auto sett = model->getCommonSettings();
    view.setDailyHealth(model->getDailySteps(), sett.dailyGoals.steps,
        model->getDailyFloors(), sett.dailyGoals.floors,
        model->getDailyActivityTime(), sett.dailyGoals.activityMinutes);
}

void SettingsWatchFacePresenter::onCommonSettings(const WatchSettings &sett)
{
    view.setDailyHealth(model->getDailySteps(), sett.dailyGoals.steps,
        model->getDailyFloors(), sett.dailyGoals.floors,
        model->getDailyActivityTime(), sett.dailyGoals.activityMinutes);
}

void SettingsWatchFacePresenter::onLocalSettings(const WatchSettingsLocal &sett)
{
    view.setClockSettings(sett.clock);
    view.setMuteAlerts(sett.alertMutedFlag);
}

void SettingsWatchFacePresenter::onBatteryLevel(uint8_t lvl)
{
    view.setBatteryLevel(lvl);
}

void SettingsWatchFacePresenter::onTime(uint8_t h, uint8_t m, uint8_t s)
{
    view.setTime(h, m, s);
}

void SettingsWatchFacePresenter::onDate(uint8_t m, uint8_t d, uint8_t wd)
{
    view.setDate(m, d, wd);
}

void SettingsWatchFacePresenter::onDailyHealth(uint32_t steps, uint32_t floors, uint32_t activityMinutes)
{
    auto sett = model->getCommonSettings();
    view.setDailyHealth(steps, sett.dailyGoals.steps,
        floors, sett.dailyGoals.floors, activityMinutes, sett.dailyGoals.activityMinutes);
}

void SettingsWatchFacePresenter::onIdleTimeout()
{
    model->application().gotoHomeScreenNoTransition();
}
