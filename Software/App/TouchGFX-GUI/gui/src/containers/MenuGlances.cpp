#include <gui/containers/MenuGlances.hpp>
#include "gui/common/GuiLogger.hpp"

MenuGlances::MenuGlances()
{

}

MenuGlances::~MenuGlances()
{
    clearGlanceControls();
}

void MenuGlances::initialize()
{
    MenuGlancesBase::initialize();

   // Default buttons state
    buttons.setL1(Buttons::NONE);
    buttons.setL2(Buttons::NONE);
    buttons.setR1(Buttons::NONE);
    buttons.setR2(Buttons::WHITE);

    wheel.setNumberOfItems(Id::COUNT);
    wheel.setCircular(false);
    sideBar.setCount(Id::COUNT);
}

void MenuGlances::setNotifications(uint32_t callsNum, uint32_t emailsNum, uint32_t msgsNum)
{
    mNotifCallsNum = callsNum;
    mNotifEmailsNum = emailsNum;
    mNotifMsgsNum = msgsNum;
    update();
}

void MenuGlances::updAppList(const std::shared_ptr<std::vector<App::GlanceData>> &list)
{
    const auto applyWheelMode = [this](int16_t itemsCount) {
        wheel.setCircular(itemsCount >= 2);
        wheel.setNumberOfItems(itemsCount);
        sideBar.setCount(itemsCount);
    };

    //mAppList = list;
    if (list == nullptr || list->empty()) {
        applyWheelMode(1); // 0 - notifications
        selectItem(0);
        clearGlanceControls();
        update();
        return;
    }

    int16_t itemsNum = static_cast<int16_t>(1 + list->size()); // 0 - notifications

    if (mWidgetsPool.empty()) {
        createGlanceControls(list);
        applyWheelMode(itemsNum);
    } else if (mWidgetsPool.size() != list->size()) {
        clearGlanceControls();
        createGlanceControls(list);
        applyWheelMode(itemsNum);
    }
    
    updateGlanceControls(list);

    update();
}


int16_t MenuGlances::getNumberOfItems()
{
    return wheel.getNumberOfItems();
}

void MenuGlances::selectNext()
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    int16_t p = wheel.getSelectedItem() + 1;
    wheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);
    sideBar.animateToId(p, Gui::Config::kMenuAnimationSteps);

    update();
}

void MenuGlances::selectPrev()
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    int16_t p = wheel.getSelectedItem() - 1;
    wheel.animateToItem(p, Gui::Config::kMenuAnimationSteps);
    sideBar.animateToId(p, Gui::Config::kMenuAnimationSteps);

    update();
}

void MenuGlances::selectItem(int16_t itemIndex)
{
    if (wheel.getNumberOfItems() <= 1) {
        return;
    }

    wheel.animateToItem(itemIndex, 0);
    sideBar.setActiveId(wheel.getSelectedItem());

    update();
}

uint16_t MenuGlances::getSelectedItem()
{
    return wheel.getSelectedItem();
}

bool MenuGlances::isNotificationSelected()
{
    return wheel.getSelectedItem() == Id::NOTIFICATION;
}

void MenuGlances::selectNextLastActivity()
{

}

void MenuGlances::wheelUpdateItem(MenuGlancesItem &item, int16_t itemIndex)
{
    if (itemIndex == Id::NOTIFICATION) {
        item.setName(T_TEXT_NOTIFICATIONS);
        return;
    }

    if (itemIndex < Id::COUNT) {
        return; // must be handled above
    }
    itemIndex -= Id::COUNT;

    // Use alternative name for glance
    if (mWidgetsPool.empty() || itemIndex >= static_cast<int16_t>(mWidgetsPool.size())) {
        return;
    }

    DrawableGlanceData& glance = mWidgetsPool.at(itemIndex);
    item.setName(glance.altname);

}

void MenuGlances::wheelUpdateCenterItem(MenuGlancesCenterItem &item, int16_t itemIndex)
{

    item.removeAll();

    if (itemIndex == Id::NOTIFICATION) {
        item.setNotifications(mNotifCallsNum, mNotifEmailsNum, mNotifMsgsNum);
        item.invalidate();
        return;
    }

    if (itemIndex < Id::COUNT) {
        return; // must be handled above
    }
    itemIndex -= Id::COUNT;

    if (itemIndex >= static_cast<int16_t>(mWidgetsPool.size())) {
        return;
    }

    for (DrawableGlanceData::ControlMapItem& mapItem : mWidgetsPool[itemIndex].controlMap) {
        item.add(*mapItem.d);
        //LOG_DEBUG("%s, %d, 0x%08X\n", mWidgetsPool[itemIndex].altname, mapItem.id, mapItem.d);
    }

    item.invalidate();
}

void MenuGlances::update()
{
    if (wheel.getSelectedItem() == NOTIFICATION/* || 
        (wheel.getSelectedItem() == LAST_ACTIVITY && mpApplist && *mpApplist->pNum > 1)*/) 
    {
        buttons.setR1(Buttons::AMBER);
    } else { 
        buttons.setR1(Buttons::NONE);
    }

    for (int i = 0; i < wheel.getNumberOfItems(); i++) {
        wheel.itemChanged(i);
    }

    wheel.invalidate();
}

void MenuGlances::clearGlanceControls()
{
    // 1) Ensure containers don't hold references to these drawables:
    // For each center item in your wheel/selected list, remove children.
    // You need access to those center item instances; e.g.:
    for (int i = 0; i < wheelSelectedListItems.getNumberOfDrawables(); ++i) {
        MenuGlancesCenterItem* center = static_cast<MenuGlancesCenterItem*>(
            wheelSelectedListItems.getDrawable(i));
        if (center) {
            center->removeAll();
        }
    }

    // 2) Now delete owned drawables
    for (auto& glance : mWidgetsPool) {
        for (DrawableGlanceData::ControlMapItem &item : glance.controlMap) {
            delete item.d;
            item.d = nullptr;
        }
        glance.controlMap.clear();
    }
    mWidgetsPool.clear();
}

void MenuGlances::createGlanceControls(const std::shared_ptr<std::vector<App::GlanceData>> &list)
{
    if (!mWidgetsPool.empty()) {
        return;
    }

    mWidgetsPool.reserve(list->size());

    for (size_t i = 0; i < list->size(); i++) {
        const auto& ctrls = (*list)[i];

        if (ctrls.count > 0) {
        
            DrawableGlanceData &newGlance = mWidgetsPool.emplace_back();

            for (uint8_t c = 0; c < ctrls.count; c++) {
                Drawable* drawable = create(ctrls.ctrls[c]);
                if (drawable) {
                    DrawableGlanceData::ControlMapItem item{};
                    item.id = ctrls.ctrls[c].id;
                    item.d = drawable;
                    newGlance.controlMap.push_back(item);
                    snprintf(newGlance.altname, sizeof(newGlance.altname), "%s", ctrls.altname);

                    //LOG_DEBUG("new control: %s, id=%d, 0x%08X\n", newGlance.altname, item.id, item.d);
                }
            }
        }
    }

    // verify
    //for (auto& glance : mWidgetsPool) {
    //    for (DrawableGlanceData::ControlMapItem& item : glance.controlMap) {
    //        LOG_DEBUG("verify     : %s, id=%d, 0x%08X\n", glance.altname, item.id, item.d);
    //    }
    //}
}

void MenuGlances::updateGlanceControls(const std::shared_ptr<std::vector<App::GlanceData>> &list)
{
    if (mWidgetsPool.empty() || mWidgetsPool.size() != list->size()) {
        return;
    }

    for (size_t i = 0; i < list->size(); i++) {
        const auto& ctrls = (*list)[i];
        DrawableGlanceData &existGlance = mWidgetsPool[i];

        for (uint8_t c = 0; c < ctrls.count; c++) {
            
            auto it = std::find_if(existGlance.controlMap.begin(), existGlance.controlMap.end(),
                [&](const DrawableGlanceData::ControlMapItem& item) {
                    return item.id == ctrls.ctrls[c].id;
                });

            if (it != existGlance.controlMap.end()) {
                DrawableGlanceData::ControlMapItem& item = *it;
                //LOG_DEBUG("upd control: %s, id=%d, 0x%08X\n", existGlance.altname, item.id, item.d);

                update(ctrls.ctrls[c], item.d);
            }
        }
    }
}

Drawable* MenuGlances::create(const GlanceControl_t& ctrl)
{
    switch (ctrl.type) {
    case GlanceType_t::GLANCE_TYPE_TEXT:
    {
        TextDrawable* w = new (std::nothrow)TextDrawable(ctrl.payload.text.pos.x, 
                                           ctrl.payload.text.pos.y,
                                           ctrl.payload.text.size.w,
                                           ctrl.payload.text.size.h,
                                           GlanceColorToRGB(ctrl.payload.text.color), 
                                           ctrl.payload.text.str,
                                           GlanceTextToTypedText(ctrl.payload.text.font, ctrl.payload.text.align));
        return w;
    } break;
    case GlanceType_t::GLANCE_TYPE_IMAGE:
    {
        ImageDrawable* w = new (std::nothrow)ImageDrawable(ctrl.payload.image.pos.x,
                                             ctrl.payload.image.pos.y, 
                                             ctrl.payload.image.size.w, 
                                             ctrl.payload.image.size.h, 
                                             ctrl.payload.image.buff);
        return w;
    } break;
    case GlanceType_t::GLANCE_TYPE_RECT:
    {
        BoxDrawable* w = new (std::nothrow)BoxDrawable(ctrl.payload.rect.pos.x,
                                         ctrl.payload.rect.pos.y,
                                         ctrl.payload.rect.size.w, 
                                         ctrl.payload.rect.size.h,
                                         GlanceColorToRGB(ctrl.payload.rect.color),
                                         GlanceColorToRGB(ctrl.payload.rect.bgColor));
        return w;
    } break;
    case GlanceType_t::GLANCE_TYPE_LINE:
    {
        LineDrawable* w = new (std::nothrow)LineDrawable(ctrl.payload.line.start.x,
                                           ctrl.payload.line.start.y,
                                           ctrl.payload.line.stop.x,
                                           ctrl.payload.line.stop.y,
                                           GlanceColorToRGB(ctrl.payload.line.color));
        return w;
    } break;
    default:
        break;
    }

    return nullptr;
}

void MenuGlances::update(const GlanceControl_t& ctrl, Drawable* d)
{
    switch (ctrl.type) {

    case GlanceType_t::GLANCE_TYPE_TEXT:
    {
        TextDrawable& w = *(reinterpret_cast<TextDrawable*>(d));
        w.update(ctrl.payload.text.pos.x,
                 ctrl.payload.text.pos.y,
                 ctrl.payload.text.size.w,
                 ctrl.payload.text.size.h,
                 GlanceColorToRGB(ctrl.payload.text.color),
                 ctrl.payload.text.str,
                 GlanceTextToTypedText(ctrl.payload.text.font, ctrl.payload.text.align));
    } break;

    case GlanceType_t::GLANCE_TYPE_IMAGE:
    {
        ImageDrawable& w = *(reinterpret_cast<ImageDrawable*>(d));

        const bool shouldBeUpdated = w.getHeight() != ctrl.payload.image.size.h ||
                                     w.getWidth()  != ctrl.payload.image.size.w ||
                                     w.getPixels() != ctrl.payload.image.buff;

        if (shouldBeUpdated) {
            w.update(ctrl.payload.image.pos.x,
                     ctrl.payload.image.pos.y,
                     ctrl.payload.image.size.w,
                     ctrl.payload.image.size.h,
                     ctrl.payload.image.buff);
        } else {
            w.setXY(ctrl.payload.image.pos.x, ctrl.payload.image.pos.y);
            w.invalidate();
        }
    } break;

    case GlanceType_t::GLANCE_TYPE_RECT:
    {
        BoxDrawable& w = *(reinterpret_cast<BoxDrawable*>(d));
        w.update(ctrl.payload.rect.pos.x,
                 ctrl.payload.rect.pos.y,
                 ctrl.payload.rect.size.w,
                 ctrl.payload.rect.size.h,
                 GlanceColorToRGB(ctrl.payload.rect.color),
                 GlanceColorToRGB(ctrl.payload.rect.bgColor));
    } break;

    case GlanceType_t::GLANCE_TYPE_LINE:
    {
        LineDrawable& w = *(reinterpret_cast<LineDrawable*>(d));
        w.update(ctrl.payload.line.start.x,
                 ctrl.payload.line.start.y,
                 ctrl.payload.line.stop.x,
                 ctrl.payload.line.stop.y,
                 GlanceColorToRGB(ctrl.payload.line.color));
    } break;

    default: return;
    }

    if (d->isVisible() != ctrl.visible) {
        d->setVisible(ctrl.visible);
        d->invalidate();
    }
}

touchgfx::colortype MenuGlances::GlanceColorToRGB(uint8_t color) const
{
    uint8_t r = (color >> 4) << 6;
    uint8_t g = ((color >> 2) & 0x03) << 6;
    uint8_t b = (color & 0x03) << 6;

    return (static_cast<uint32_t>(r) << 16) |
        (static_cast<uint32_t>(g) << 8) |
        (static_cast<uint32_t>(b));
}

const touchgfx::TypedText MenuGlances::GlanceTextToTypedText(uint8_t font, uint8_t align) const
{
    constexpr TEXTS lut[GLANCE_FONTS_COUNT]
    {
        T_TMP_REGULAR_18,
        T_TMP_MEDIUM_10,
        T_TMP_MEDIUM_18,
        T_TMP_MEDIUM_25,
        T_TMP_SEMIBOLD_18,
        T_TMP_SEMIBOLD_20,
        T_TMP_SEMIBOLD_25,
        T_TMP_SEMIBOLD_30,
        T_TMP_SEMIBOLD_35,
        T_TMP_ITALIC_18,
        T_TMP_ITALIC_20,
    };

    constexpr TEXTS lutr[GLANCE_FONTS_COUNT]
    {
        T_TMP_REGULAR_18_R,
        T_TMP_MEDIUM_10_R,
        T_TMP_MEDIUM_18_R,
        T_TMP_MEDIUM_25_R,
        T_TMP_SEMIBOLD_18_R,
        T_TMP_SEMIBOLD_20_R,
        T_TMP_SEMIBOLD_25_R,
        T_TMP_SEMIBOLD_30_R,
        T_TMP_SEMIBOLD_35_R,
        T_TMP_ITALIC_18_R,
        T_TMP_ITALIC_20_R,
    };

    constexpr TEXTS lutl[GLANCE_FONTS_COUNT]
    {
        T_TMP_REGULAR_18_L,
        T_TMP_MEDIUM_10_L,
        T_TMP_MEDIUM_18_L,
        T_TMP_MEDIUM_25_L,
        T_TMP_SEMIBOLD_18_L,
        T_TMP_SEMIBOLD_20_L,
        T_TMP_SEMIBOLD_25_L,
        T_TMP_SEMIBOLD_30_L,
        T_TMP_SEMIBOLD_35_L,
        T_TMP_ITALIC_18_L,
        T_TMP_ITALIC_20_L,
    };

    switch (align) {
    case GLANCE_ALIGN_H_LEFT:
        return touchgfx::TypedText(lutl[font]);
    case GLANCE_ALIGN_H_CENTER: 
        return touchgfx::TypedText(lut[font]);
    case GLANCE_ALIGN_H_RIGHT: 
        return touchgfx::TypedText(lutr[font]);
    default:
        return touchgfx::TypedText(lut[0]);
    }
}
