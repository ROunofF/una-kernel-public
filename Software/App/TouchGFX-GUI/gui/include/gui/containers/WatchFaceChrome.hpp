#ifndef WATCHFACECHROME_HPP
#define WATCHFACECHROME_HPP

#include <gui_generated/containers/WatchFaceChromeBase.hpp>
#include <gui/containers/CountdownTimer.hpp>

/**
 * @brief   Says where in the list the face on screen is, and what the two
 *          right keys do.
 * @note    It sits over the face it is describing, so it stays up only while
 *          it has something to say and then gets out of the way. Whoever shows
 *          it only has to say that something happened.
 */
class WatchFaceChrome : public WatchFaceChromeBase
{
public:
    WatchFaceChrome();
    virtual ~WatchFaceChrome() {}

    virtual void initialize();

    /** @brief Come up, and start the wait over. */
    void show();

    /**
     * @brief Say how many faces there are to step through and which one is up.
     * @note  A count of zero leaves the position blank, which is what to show
     *        while the list is still on its way.
     */
    void setCursor(uint8_t count, uint8_t index);

    /**
     * @brief Move the position one place.
     * @param delta -1 for the previous face, +1 for the next.
     */
    void stepCursor(int8_t delta);

protected:
    CountdownTimer                      mHideTimer;
    touchgfx::Callback<WatchFaceChrome> mHideCb;

    void onHide();
};

#endif // WATCHFACECHROME_HPP
