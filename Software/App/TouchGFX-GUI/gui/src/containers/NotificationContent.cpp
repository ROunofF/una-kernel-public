#include <gui/containers/NotificationContent.hpp>

NotificationContent::NotificationContent()
{

}

void NotificationContent::initialize()
{
    NotificationContentBase::initialize();
    mOrigPosition = body.getY();
}

void NotificationContent::setContent(TypedTextId msgId)
{
    Unicode::snprintf(bodyBuffer, BODY_SIZE, "%s", touchgfx::TypedText(msgId).getText());
}

void NotificationContent::setContent(const uint8_t *subtitle, const uint8_t *msg)
{
    uint16_t len = 0;

    if (subtitle) {
        len = Unicode::fromUTF8(subtitle, bodyBuffer, BODY_SIZE);
    }

    if (len > 0 && msg) {
        // Change '\0' with '\n\0'
        Unicode::fromUTF8(reinterpret_cast <const uint8_t *>("\n"), &bodyBuffer[len - 1], BODY_SIZE - len - 1);
        len++;
    }

    if (len > 0) {
        len--; // Remove last '\0'
    }

    if (len < BODY_SIZE && msg) {
        Unicode::fromUTF8(msg, &bodyBuffer[len], BODY_SIZE - len);
    }

    bodyBuffer[BODY_SIZE - 1] = 0; // ensure string termination
    body.resizeHeightToCurrentText();
    body.invalidate();
}

int16_t NotificationContent::getNumberOfItems()
{
    int16_t step = body.getTypedText().getFont()->getHeight() + body.getLinespacing();

    int16_t scrollSteps = 0;
    if (body.getHeight() > this->getHeight()) {
        scrollSteps = (body.getHeight() - this->getHeight()) / step;
    }

    return scrollSteps + 1;
}

void NotificationContent::selectNext()
{
    selectItem(mIndex + 1);
}

void NotificationContent::selectPrev()
{
    selectItem(mIndex - 1);
}

void NotificationContent::selectItem(int16_t itemIndex)
{
    if (getNumberOfItems() > itemIndex && itemIndex >= 0) {
        int16_t step = body.getTypedText().getFont()->getHeight() + body.getLinespacing();
        body.setY(mOrigPosition - (itemIndex * step) + body.getLinespacing());
        body.invalidate();
        mIndex = itemIndex;
    }
}

int16_t NotificationContent::getSelectedItem()
{
    return mIndex;
}

