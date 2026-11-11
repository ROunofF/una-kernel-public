#ifndef ACTIVITYBARTOP_HPP
#define ACTIVITYBARTOP_HPP

#include <gui_generated/containers/ActivityBarTopBase.hpp>

class ActivityBarTop : public ActivityBarTopBase
{
public:
    ActivityBarTop();
    virtual ~ActivityBarTop() {}

    virtual void initialize();

    enum class Type {
        TIME,
        RUNNING,
        STEP
    };

    void setType(Type type);
    void setProgress(uint8_t pct);

protected:
};

#endif // ACTIVITYBARTOP_HPP
