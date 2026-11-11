#ifndef SETTINGSWATCHFACEPRESENTER_HPP
#define SETTINGSWATCHFACEPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

#include <array>

using namespace touchgfx;

class SettingsWatchFaceView;

class SettingsWatchFacePresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsWatchFacePresenter(SettingsWatchFaceView& v);

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

    virtual ~SettingsWatchFacePresenter() {}

    /// Step to the previous / next face, putting it on screen.
    void selectPrev();
    void selectNext();

    /// Store the face that is on screen as the one the watch face uses.
    void apply();

    virtual void onAppIds(App::Type type,
                          const std::shared_ptr<std::vector<uint64_t>> &ids) override;

    virtual void onWatchFaceRunning(uint32_t guiPid) override;

    virtual void onCommonSettings(const WatchSettings &sett) override;
    virtual void onLocalSettings(const WatchSettingsLocal &sett) override;
    virtual void onBatteryLevel(uint8_t lvl) override;
    virtual void onTime(uint8_t h, uint8_t m, uint8_t s) override;
    virtual void onDate(uint8_t m, uint8_t d, uint8_t wd) override;
    virtual void onDailyHealth(uint32_t steps, uint32_t floors, uint32_t activityMinutes) override;

    virtual void onIdleTimeout() override;

private:
    SettingsWatchFacePresenter();

    SettingsWatchFaceView& view;

    /// The built-in face plus the installed ones. Position 0 is always the
    /// built-in face, which is not an app and is held as id 0. Faces past the
    /// limit are left out of the picker. At most 255: the count and the index
    /// are uint8_t.
    static constexpr uint8_t kMaxFaces = 100;
    std::array<uint64_t, kMaxFaces> mFaceIds {};

    /// Faces in mFaceIds. Zero until the list arrives, which is a few ticks
    /// after the screen opens; stepping is refused until then rather than
    /// guessed at, since the face on screen is already the right one.
    uint8_t mFaceCount = 0;

    uint8_t mIndex = 0;

    /// The face on screen. Leads mIndex: it is known from the settings as soon
    /// as the screen opens, while the position it sits at is only known once
    /// the list has arrived.
    uint64_t mSelectedId = 0;

    void showSelected();
    void updateFace();
};

#endif // SETTINGSWATCHFACEPRESENTER_HPP
