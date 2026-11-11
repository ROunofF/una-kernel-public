#include <gui/containers/ProgressBar.hpp>

ProgressBar::ProgressBar()
{

}

void ProgressBar::initialize()
{
    ProgressBarBase::initialize();
}

void ProgressBar::setProgress(float pct)
{
    float end = (pct * 360) / 100;
    progress.setArc<float>(0, end);
    progress.invalidate();
    background.invalidate();
}

void ProgressBar::setColor(touchgfx::colortype color)
{
    progressPainter.setColor(color);
    progress.invalidate();
    background.invalidate();
}