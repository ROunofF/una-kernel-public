#include <gui/containers/ActivityBarTop.hpp>

ActivityBarTop::ActivityBarTop()
{

}

void ActivityBarTop::initialize()
{
    ActivityBarTopBase::initialize();
}

void ActivityBarTop::setType(Type type)
{
    iconTime.setVisible(false);
    iconMan.setVisible(false);
    iconStep.setVisible(false);

    switch (type) {
        case Type::TIME:
            iconTime.setVisible(true);
            break;
        case Type::RUNNING:
            iconMan.setVisible(true);
            break;
        case Type::STEP:
            iconStep.setVisible(true);
            break;
        default:
            break;
    }

    iconTime.invalidate();
    iconMan.invalidate();
    iconStep.invalidate();
}

void ActivityBarTop::setProgress(uint8_t pct)
{
    float min = 0;
    float max = 0;
    progress.getArcStart(min);
    background.getArcEnd(max);

    float end = min + ((max - min) * pct) / 100;
    progress.setArc(min, end);

    progress.invalidate();
    background.invalidate();
}