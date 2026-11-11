#ifndef MENUSETTIME_HPP
#define MENUSETTIME_HPP

#include <gui_generated/containers/MenuSetTimeBase.hpp>

class MenuSetTime : public MenuSetTimeBase
{
public:
    MenuSetTime();
    virtual ~MenuSetTime() {}

    virtual void initialize();

    /**
     * @brief   Show the hours as 12, 1 .. 11 rather than 00 .. 23.
     * @note    The hour given to and taken from setTime() / getTime() is then
     *          0 .. 11, with 0 drawn as 12; AM or PM is the caller's to keep.
     *          Call before setTime().
     */
    void set12Hour(bool on);

    void setTime(uint8_t h, uint8_t m);
    void getTime(uint8_t &h, uint8_t &m);

    void setActiveHours();
    void setActiveMinutes();

    void incValue();
    void decValue();
protected:

    bool mMinutesAactive {};
    bool m12Hour {};

    virtual void hoursWheelUpdateItem(MenuSetTimeItemHoursNotSelected &item, int16_t itemIndex) override;
    virtual void hoursWheelUpdateCenterItem(MenuSetTimeItemHoursSelected &item, int16_t itemIndex) override;
    virtual void minutesWheelUpdateItem(MenuSetTimeItemMinutesNotSelected &item, int16_t itemIndex) override;
    virtual void minutesWheelUpdateCenterItem(MenuSetTimeItemMinutesSelected &item, int16_t itemIndex) override;

    void setHours(int16_t h);
    void setMinutes(int16_t m);

    /// What hour index @p h is drawn as.
    int16_t displayHour(int16_t h) const
    {
        return (m12Hour && h == 0) ? 12 : h;
    }
};

#endif // MENUSETTIME_HPP
