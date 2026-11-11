#include <gui/containers/BatterySmall.hpp>

BatterySmall::BatterySmall()
{

}

void BatterySmall::initialize()
{
    BatterySmallBase::initialize();
}

void BatterySmall::setBatteryLevel(uint8_t level)
{
    /**
     *  Level     s1          s2      s3      s4
     *  0 %       off         off     off     off
     *  1-24 %    red         off     off     off
     *  25-49 %   teal        teal    off     off
     *  50-74 %   teal        teal    teal    off
     *  75-100 %  teal        teal    teal    teal
     */

    battery0.setVisible(false);
    battery25.setVisible(false);
    battery50.setVisible(false);
    battery75.setVisible(false);
    battery100.setVisible(false);

    if (level >= 75) {
        battery100.setVisible(true);
    } else if (level >= 50) {
        battery75.setVisible(true);
    } else if (level >= 25) {
        battery50.setVisible(true);
    } else if (level >= 1) {
        battery25.setVisible(true);
    } else {
        battery0.setVisible(true);
    }

    battery0.invalidate();
    battery25.invalidate();
    battery50.invalidate();
    battery75.invalidate();
    battery100.invalidate();
}
