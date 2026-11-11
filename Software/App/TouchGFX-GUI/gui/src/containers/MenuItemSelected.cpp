#include <gui/containers/MenuItemSelected.hpp>
#include <texts/TextKeysAndLanguages.hpp>

MenuItemSelected::MenuItemSelected()
{

}

MenuItemSelected::~MenuItemSelected()
{
    // Delete bitmap
    if (mDynamicIconId != BITMAP_INVALID) {
        Bitmap::dynamicBitmapDelete(mDynamicIconId);
    }
}

void MenuItemSelected::initialize()
{
    MenuItemSelectedBase::initialize();

    reset();
    mMsgIdType = text.getTypedText().getId();
    mMsgIdTipType = textTip.getTypedText().getId();
}

void MenuItemSelected::config(TypedTextId msgId)
{
    reset();

    mMsgId = msgId;
    mStyle = STYLE_SIMPLE;
}

void MenuItemSelected::config(touchgfx::Unicode::UnicodeChar *msg)
{
    reset();

    if (msg != nullptr) {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", msg);
    }
    mStyle = STYLE_SIMPLE;
}

void MenuItemSelected::configToggle(TypedTextId msgId)
{
    reset();

    mMsgId = msgId;
    mStyle = STYLE_TOGGLE;
}

void MenuItemSelected::configToggle(touchgfx::Unicode::UnicodeChar *msg)
{
    reset();

    if (msg != nullptr) {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", msg);
    }
    mStyle = STYLE_TOGGLE;
}

void MenuItemSelected::configTip(TypedTextId msgId, TypedTextId msgIdTip)
{
    reset();

    mMsgId = msgId;
    mMsgIdTip = msgIdTip;
    mStyle = STYLE_TIP;
}

void MenuItemSelected::configTip(TypedTextId msgId, touchgfx::Unicode::UnicodeChar *msgTip)
{
    reset();

    mMsgId = msgId;
    if (msgTip != nullptr) {
        Unicode::snprintf(textTipBuffer, TEXTTIP_SIZE, "%s", msgTip);
    }
    mStyle = STYLE_TIP;
}

void MenuItemSelected::configIcon(BitmapId iconId, TypedTextId msgId)
{
    reset();

    mIconId = iconId;
    mMsgId = msgId;
    mStyle = STYLE_ICON;
}

void MenuItemSelected::configIcon(BitmapId iconId, touchgfx::Unicode::UnicodeChar *msg)
{
    reset();

    mIconId = iconId;
    if (msg != nullptr) {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", msg);
    }
    mStyle = STYLE_ICON;
}

void MenuItemSelected::configIcon(const uint8_t *pExtIcon, touchgfx::Unicode::UnicodeChar *msg)
{
    reset();

    mpExtIcon = pExtIcon;

    if (msg != nullptr) {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", msg);
    }
    mStyle = STYLE_ICON_EXT;
}

void MenuItemSelected::setMsgTypedTextId(TypedTextId msgIdType)
{
    if (msgIdType != TYPED_TEXT_INVALID) {
        mMsgIdType = msgIdType;
    } else {
        mMsgIdType = T_TMP_SEMIBOLD_30;
    }
}

void MenuItemSelected::setTipTypedTextId(TypedTextId msgIdType)
{
    if (msgIdType != TYPED_TEXT_INVALID) {
        mMsgIdTipType = msgIdType;
    } else {
        mMsgIdTipType = T_TMP_ITALIC_18;
    }
}

void MenuItemSelected::setToggle(bool state)
{
    toggleSwitch.setState(state);
}

bool MenuItemSelected::getToggle()
{
    return toggleSwitch.getState();
}

void MenuItemSelected::reset()
{
    mStyle = STYLE_SIMPLE;

    mMsgId = TYPED_TEXT_INVALID;
    mMsgIdType = T_TMP_SEMIBOLD_30;
    textBuffer[0] = 0;

    mMsgIdTip = TYPED_TEXT_INVALID;
    mMsgIdTipType = T_TMP_ITALIC_18;
    textTipBuffer[0] = 0;


    mIconId = BITMAP_INVALID;
    mpExtIcon = nullptr;
}

void MenuItemSelected::alignTextY(TextArea &t) const
{
    t.resizeHeightToCurrentText();
    int16_t h = t.getHeight();
    t.setY((getHeight() - h) / 2);
}

void MenuItemSelected::updStyle()
{
    if (mMsgId != TYPED_TEXT_INVALID) {
        Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", touchgfx::TypedText(mMsgId).getText());
    }

    if (mMsgIdTip != TYPED_TEXT_INVALID) {
        Unicode::snprintf(textTipBuffer, TEXTTIP_SIZE, "%s", touchgfx::TypedText(mMsgIdTip).getText());
    }

    if (mMsgIdType != TYPED_TEXT_INVALID) {
        text.setTypedText(touchgfx::TypedText(mMsgIdType));
    }

    if (mMsgIdTipType != TYPED_TEXT_INVALID) {
        textTip.setTypedText(touchgfx::TypedText(mMsgIdTipType));
    }

    if (mIconId != BITMAP_INVALID) {
        icon.setBitmap(touchgfx::Bitmap(mIconId));
    } else if (mpExtIcon != nullptr ) {
        // Delete previous bitmap
        if (mDynamicIconId != BITMAP_INVALID) { 
            Bitmap::dynamicBitmapDelete(mDynamicIconId);
        }
        // Create (16bit) dynamic bitmap
        mDynamicIconId = Bitmap::dynamicBitmapCreateExternal(App::kIconWH, App::kIconWH, mpExtIcon, Bitmap::ABGR2222);
        icon.setBitmap(Bitmap(mDynamicIconId));
    }

    switch (mStyle) {
        case STYLE_SIMPLE:
            text.setVisible(true);
            text.setWidth(getWidth());
            text.setX(0);
            alignTextY(text);

            toggleSwitch.setVisible(false);
            textTip.setVisible(false);
            icon.setVisible(false);
            break;

        case STYLE_TOGGLE:
            text.setWidth(130);
            text.setX(21);
            alignTextY(text);

            toggleSwitch.setVisible(true);
            textTip.setVisible(false);
            icon.setVisible(false);
            break;

        case STYLE_TIP:
            text.setVisible(true);
            text.setWidth(getWidth());
            text.setX(0);
            text.setY(2);

            toggleSwitch.setVisible(false);
            textTip.setVisible(true);
            icon.setVisible(false);
            break;

        case STYLE_ICON:
        case STYLE_ICON_EXT:
            text.setVisible(true);
            text.setWidth(153);
            text.setX(87);
            alignTextY(text);

            toggleSwitch.setVisible(false);
            textTip.setVisible(false);
            icon.setVisible(true);
            break;

        default:  
            break;
    }

    text.invalidate();
    textTip.invalidate();
    toggleSwitch.invalidate();
    icon.invalidate();
}

MenuItemSelected &MenuItemSelected::operator=(const MenuItemSelected &other)
{
    if (this != &other) {
        mStyle = other.mStyle;

        mMsgId = other.mMsgId;
        mMsgIdType = other.mMsgIdType;
        Unicode::strncpy(textBuffer, other.textBuffer, TEXT_SIZE);

        mMsgIdTip = other.mMsgIdTip;
        mMsgIdTipType = other.mMsgIdTipType;
        Unicode::strncpy(textTipBuffer, other.textTipBuffer, TEXTTIP_SIZE);

        mIconId = other.mIconId;
        mpExtIcon = other.mpExtIcon;

        toggleSwitch.setState(other.toggleSwitch.getState());

        updStyle();
    }
    return *this;
}
