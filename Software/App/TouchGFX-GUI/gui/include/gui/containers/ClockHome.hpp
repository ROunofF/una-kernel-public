#ifndef CLOCKHOME_HPP
#define CLOCKHOME_HPP

#include <gui_generated/containers/ClockHomeBase.hpp>

class ClockHome : public ClockHomeBase
{
public:
    ClockHome();
    virtual ~ClockHome() {}

    virtual void initialize();

    void setTime(uint8_t h, uint8_t m, uint8_t s);
    void setDate(uint8_t m, uint8_t d, uint8_t wd);
    void setFormat(WatchSettingsLocal::Clock::Format format);
    void setSeconds(bool state);
    void setDateOrder(WatchSettingsLocal::Clock::DateOrder order);

protected:
    /// Time and date are kept so that a format change can redraw both lines
    /// without waiting for the next event from the backend.
    uint8_t mHour = 0;
    uint8_t mMinute = 0;
    uint8_t mSecond = 0;
    uint8_t mMonth = 0;
    uint8_t mDay = 0;
    uint8_t mWeekDay = 0;

    WatchSettingsLocal::Clock::Format mFormat {};
    bool mSeconds = false;
    WatchSettingsLocal::Clock::DateOrder mDateOrder {};

    void updateTime();
    void updateDate();
};

#endif // CLOCKHOME_HPP
