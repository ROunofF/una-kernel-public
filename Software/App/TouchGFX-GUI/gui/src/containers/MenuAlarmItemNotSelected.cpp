#include <gui/containers/MenuAlarmItemNotSelected.hpp>

MenuAlarmItemNotSelected::MenuAlarmItemNotSelected()
{

}

void MenuAlarmItemNotSelected::initialize()
{
    MenuAlarmItemNotSelectedBase::initialize();

    reset();
}

void MenuAlarmItemNotSelected::config(TypedTextId msgId)
{
    reset();

    mMsgId = msgId;
    mStyle = STYLE_SIMPLE;
}

void MenuAlarmItemNotSelected::reset()
{
    mStyle = STYLE_SIMPLE;

    mMsgId = TYPED_TEXT_INVALID;
    textBuffer[0] = 0;
}

void MenuAlarmItemNotSelected::updStyle()
{
    if (mMsgId != TYPED_TEXT_INVALID) {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", touchgfx::TypedText(mMsgId).getText());
    }

    switch (mStyle) {
        case STYLE_SIMPLE:
            text.setVisible(true);
            break;
        default:
            break;
    }

    text.invalidate();
}

MenuAlarmItemNotSelected &MenuAlarmItemNotSelected::operator=(const MenuAlarmItemNotSelected &other)
{
    if (this != &other) {
        mStyle = other.mStyle;

        mMsgId = other.mMsgId;
        Unicode::strncpy(textBuffer, other.textBuffer, TEXT_SIZE);

        updStyle();
    }
    return *this;
}