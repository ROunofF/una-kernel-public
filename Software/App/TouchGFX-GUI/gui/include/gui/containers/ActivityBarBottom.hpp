#ifndef ACTIVITYBARBOTTOM_HPP
#define ACTIVITYBARBOTTOM_HPP

#include <gui_generated/containers/ActivityBarBottomBase.hpp>

class ActivityBarBottom : public ActivityBarBottomBase
{
public:
    ActivityBarBottom();
    virtual ~ActivityBarBottom() {}

    virtual void initialize();

    enum class Type {
        TIME,
        RUNNING,
        STEP,
        WIDGET      // app-pushed widget: centre label (msg) instead of an icon
    };

    void setType(Type type);
    void setMsg(const char *text);
    // fromEnd pins the fill to the end so it drains from the start as pct falls.
    void setProgress(uint8_t pct, bool fromEnd = false);

protected:
};

#endif // ACTIVITYBARBOTTOM_HPP
