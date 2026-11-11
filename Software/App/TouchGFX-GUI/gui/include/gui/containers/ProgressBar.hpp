#ifndef PROGRESSBAR_HPP
#define PROGRESSBAR_HPP

#include <gui_generated/containers/ProgressBarBase.hpp>
#include "touchgfx/Color.hpp"

class ProgressBar : public ProgressBarBase
{
public:
    ProgressBar();
    virtual ~ProgressBar() {}

    virtual void initialize();

    void setProgress(float pct);
    void setColor(touchgfx::colortype color);
protected:
};

#endif // PROGRESSBAR_HPP
