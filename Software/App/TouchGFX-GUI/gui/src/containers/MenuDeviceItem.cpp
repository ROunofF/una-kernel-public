#include <gui/containers/MenuDeviceItem.hpp>

MenuDeviceItem::MenuDeviceItem()
{

}

void MenuDeviceItem::initialize()
{
    MenuDeviceItemBase::initialize();
}

void MenuDeviceItem::setTitle(TypedTextId msgId)
{
    Unicode::snprintf(titleBuffer, TITLE_SIZE, "%s", touchgfx::TypedText(msgId).getText());
    title.invalidate();
}

void MenuDeviceItem::setValue(touchgfx::Unicode::UnicodeChar *msg)
{
    Unicode::snprintf(valueBuffer, VALUE_SIZE, "%s", msg);
    value.invalidate();
}

void MenuDeviceItem::setValue(const char *msg)
{
    Unicode::fromUTF8(reinterpret_cast<const uint8_t *>(msg), valueBuffer, VALUE_SIZE);
    value.invalidate();
}

MenuDeviceItem &MenuDeviceItem::operator=(const MenuDeviceItem &other)
{
    if (this != &other) {
        int16_t h = 0;

        Unicode::snprintf(titleBuffer, TITLE_SIZE, "%s", other.titleBuffer);
        // Adjust position
        title.resizeHeightToCurrentText();
        h = title.getHeight();
        title.setY(getHeight()/2 - h);

        Unicode::snprintf(valueBuffer, VALUE_SIZE, "%s", other.valueBuffer);
    }
    return *this;
}