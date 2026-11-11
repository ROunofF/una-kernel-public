#include <gui/containers/MenuGlancesItem.hpp>

MenuGlancesItem::MenuGlancesItem()
{

}

void MenuGlancesItem::initialize()
{
    MenuGlancesItemBase::initialize();
}

void MenuGlancesItem::setName(TypedTextId id)
{
    Unicode::snprintf(textBuffer, TEXT_SIZE, "%s", touchgfx::TypedText(id).getText());
    text.invalidate();
}

void MenuGlancesItem::setName(const char* name)
{
    Unicode::fromUTF8(reinterpret_cast<const uint8_t*>(name), textBuffer, TEXT_SIZE);
    text.invalidate();
}