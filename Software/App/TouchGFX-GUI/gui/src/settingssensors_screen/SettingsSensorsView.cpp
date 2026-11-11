#include <gui/settingssensors_screen/SettingsSensorsView.hpp>

#include <cstdio>
#include <cstring>

#include <BitmapDatabase.hpp>
#include <touchgfx/Font.hpp>
#include <gui/common/GuiConfig.hpp>

namespace {
// Widths (px) the name must fit before it gets an ellipsis: the selection pill
// on the list, and the result subtitle on the Saved panel.
constexpr uint16_t kPillWidth   = 200;
constexpr uint16_t kResultWidth = 200;

// "Searching..." dot animation cadence.
constexpr uint16_t kDotPeriod = GUI_CONFIG_MS_2_TICKS(400);
constexpr uint16_t kScreenWidth = 240;
constexpr uint16_t kScreenHeight = 240;

// Truncate s in place with a trailing "..." so it fits maxW at the given
// font (pixel-accurate, matching the design rather than a fixed char count).
void truncToWidth(touchgfx::Unicode::UnicodeChar* s, uint16_t cap,
        uint16_t maxW, TypedTextId fontId)
{
    const touchgfx::Font* font = touchgfx::TypedText(fontId).getFont();
    if (font == nullptr || font->getStringWidth(s) <= maxW) {
        return;
    }
    for (int len = touchgfx::Unicode::strlen(s); len > 0; --len) {
        int n = len;
        while (n > 0 && s[n - 1] == ' ') {   // drop a trailing space before "..."
            --n;
        }
        if (static_cast<uint16_t>(n + 4) > cap) {
            continue;
        }
        touchgfx::Unicode::UnicodeChar cand[24];
        touchgfx::Unicode::strncpy(cand, s, cap);
        cand[n] = '.'; cand[n + 1] = '.'; cand[n + 2] = '.'; cand[n + 3] = 0;
        if (font->getStringWidth(cand) <= maxW) {
            touchgfx::Unicode::strncpy(s, cand, cap);
            return;
        }
    }
}

// Build the "Devices" row value shown on the Sensors screen: the saved-strap
// count, or "None" when nothing is remembered yet.
void devicesTip(const B2GEvent::AccessoryConfig& cfg,
        touchgfx::Unicode::UnicodeChar* out, uint16_t size)
{
    if (cfg.count == 0) {
        touchgfx::Unicode::strncpy(out, "None", size);
    } else {
        touchgfx::Unicode::snprintf(out, size, "%u Saved",
                static_cast<unsigned>(cfg.count));
    }
}

// Value text for the External HR row — the three source modes
// (WatchSettingsAccessories::Mode).
const char* modeLabel(uint8_t mode)
{
    switch (mode) {
        case 1:  return "External & Wrist";
        case 2:  return "External Only";
        default: return "Off";
    }
}
} // namespace

SettingsSensorsView::SettingsSensorsView()
{
    menu.setTitle(T_TEXT_SENSORS_UC);
    rebuild();
}

void SettingsSensorsView::setupScreen()
{
    SettingsSensorsViewBase::setupScreen();
    // Let the result subtitle wrap onto a second line (e.g. "Device has been
    // deleted") instead of clipping.
    resultSub.setWideTextAction(touchgfx::WIDE_TEXT_WORDWRAP);
    resultSub.setHeight(56);
    // Always (re)enter at the top of the flow, list visible, no result panel.
    mMode = Mode::Sensors;
    menu.setTitle(T_TEXT_SENSORS_UC);
    hideResult();
    rebuild();
}

void SettingsSensorsView::tearDownScreen()
{
    SettingsSensorsViewBase::tearDownScreen();
}

void SettingsSensorsView::setPositionId(uint16_t id)
{
    menu.selectItem(id);
}

uint16_t SettingsSensorsView::getPositionId()
{
    return menu.getSelectedItem();
}

uint16_t SettingsSensorsView::getSensorsRow()
{
    return (mMode == Mode::Sensors) ? menu.getSelectedItem() : 0;
}

void SettingsSensorsView::showConfig(const B2GEvent::AccessoryConfig& cfg)
{
    mConfig = cfg;
    // The list screens reflect config changes live; confirmation states hold
    // their message until they auto-dismiss.
    if (mMode == Mode::Sensors || mMode == Mode::Devices) {
        uint16_t sel = menu.getSelectedItem();
        rebuild();
        menu.selectItem(sel);   // Menu clamps out-of-range selections
    }
}

void SettingsSensorsView::showScanResults(
        const std::vector<B2GEvent::AccessoryScanResult>& results)
{
    mScan = results;
    if (mMode == Mode::Scan) {
        updateScan();
    }
}

void SettingsSensorsView::configName(uint16_t id, const char* name)
{
    touchgfx::Unicode::UnicodeChar buf[24];
    touchgfx::Unicode::strncpy(buf, (name && name[0]) ? name : "Unknown", 24);
    truncToWidth(buf, 24, kPillWidth, T_MENU_ITEM_S_TMP);
    menu.getSelectedItem(id)->config(buf);
    menu.getNotSelectedItem(id)->config(buf);
}

void SettingsSensorsView::showResult(BitmapId icon, bool confirm)
{
    menu.showListContent(false);

    resultIcon.setBitmap(touchgfx::Bitmap(icon));
    resultIcon.setVisible(true);
    resultIcon.invalidate();

    // Centered main text above the icon (restore alignment/box in case the
    // Searching state left it left-aligned and re-anchored).
    resultText.invalidate();
    resultText.setTypedText(touchgfx::TypedText(T_TMP_SEMIBOLD_30));
    resultText.setPosition(20, 47, 200, 38);
    resultText.setVisible(true);
    resultText.invalidate();

    resultSub.invalidate();   // visibility set by the caller
    // Tick (R1) / cross (R2) glyphs only on the Delete? question.
    confirmTick.setVisible(confirm);
    confirmTick.invalidate();
    confirmCross.setVisible(confirm);
    confirmCross.invalidate();
}

void SettingsSensorsView::setSearchingText()
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "Searching%.*s", static_cast<int>(mDots), "...");
    touchgfx::Unicode::strncpy(resultTextBuffer, buf, RESULTTEXT_SIZE);
}

void SettingsSensorsView::showSearching()
{
    menu.showListContent(false);
    resultIcon.setVisible(false);
    resultIcon.invalidate();
    resultSub.setVisible(false);
    resultSub.invalidate();
    confirmTick.setVisible(false);
    confirmTick.invalidate();
    confirmCross.setVisible(false);
    confirmCross.invalidate();

    // Left-aligned and anchored so the word "Searching" stays put while the
    // trailing dots animate. Anchor X centres the full 3-dot string.
    resultText.invalidate();
    resultText.setTypedText(touchgfx::TypedText(T_TMP_SEMIBOLD_30_L));
    const touchgfx::Font* font = touchgfx::TypedText(T_TMP_SEMIBOLD_30).getFont();
    const touchgfx::Unicode::UnicodeChar full[] = {
        'S','e','a','r','c','h','i','n','g','.','.','.', 0 };
    uint16_t w = (font != nullptr) ? font->getStringWidth(full) : 160;
    int16_t x = static_cast<int16_t>((kScreenWidth - w) / 2);
    // Vertically centre the 38 px-tall text box on the full 240 px screen.
    constexpr int16_t kSearchTextH = 38;
    int16_t y = static_cast<int16_t>((kScreenHeight - kSearchTextH) / 2);
    resultText.setPosition(x, y, static_cast<int16_t>(kScreenWidth - x), kSearchTextH);

    mDots = 3;
    mDotTicks = 0;
    setSearchingText();
    resultText.setVisible(true);
    resultText.invalidate();
}

void SettingsSensorsView::hideResult()
{
    resultIcon.setVisible(false);
    resultIcon.invalidate();
    resultText.setVisible(false);
    resultText.invalidate();
    resultSub.setVisible(false);
    resultSub.invalidate();
    confirmTick.setVisible(false);
    confirmTick.invalidate();
    confirmCross.setVisible(false);
    confirmCross.invalidate();
    menu.showListContent(true);
}

void SettingsSensorsView::setMode(Mode mode)
{
    mMode = mode;

    switch (mode) {
    case Mode::Sensors:
        menu.setTitle(T_TEXT_SENSORS_UC);
        hideResult();
        rebuild();
        menu.snapToItem(0);
        break;
    case Mode::Devices:
        menu.setTitle(T_TEXT_DEVICES_UC);
        hideResult();
        rebuild();
        menu.snapToItem(0);
        break;
    case Mode::Scan:
        menu.setTitle(T_TEXT_ADD_NEW_UC);
        updateScan();
        break;
    case Mode::ConfirmDelete:
        // "Delete?" — red cross icon; R1 (tick) confirms, R2 (cross) cancels.
        menu.setTitle(T_TEXT_DELETE_UC);
        touchgfx::Unicode::strncpy(resultTextBuffer, "Delete?", RESULTTEXT_SIZE);
        resultSub.setVisible(false);
        showResult(BITMAP_MENUASSETS_CIRCLECROSS_ID, true);
        break;
    case Mode::Deleted:
        menu.setTitle(T_TEXT_DELETE_UC);
        touchgfx::Unicode::strncpy(resultTextBuffer,
                touchgfx::TypedText(T_TEXT_DELETED).getText(), RESULTTEXT_SIZE);
        touchgfx::Unicode::strncpy(resultSubBuffer, "Device has been deleted", RESULTSUB_SIZE);
        resultSub.setVisible(true);
        showResult(BITMAP_MENUASSETS_CIRCLECROSS_ID, false);
        break;
    case Mode::Saved:
        menu.setTitle(T_TEXT_ADD_NEW_UC);
        touchgfx::Unicode::strncpy(resultTextBuffer,
                touchgfx::TypedText(T_TEXT_SAVED).getText(), RESULTTEXT_SIZE);
        touchgfx::Unicode::strncpy(resultSubBuffer, mResultName, RESULTSUB_SIZE);
        truncToWidth(resultSubBuffer, RESULTSUB_SIZE, kResultWidth, T_TMP_SEMIBOLD_20);
        resultSub.setVisible(true);
        showResult(BITMAP_MENUASSETS_CIRCLETICK_ID, false);
        break;
    }
}

void SettingsSensorsView::rebuild()
{
    switch (mMode) {
    case Mode::Sensors: {
        // [0] External HR mode (Off / External & Wrist / External Only),
        // [1] Devices (saved-strap count), [2] Compass.
        // The compass row is not gated: a unit without one is a fault.
        menu.setNumberOfItems(3);

        touchgfx::Unicode::UnicodeChar modeTip[24];
        touchgfx::Unicode::strncpy(modeTip, modeLabel(mConfig.mode), 24);
        menu.getSelectedItem(0)->configTip(T_TEXT_EXTERNAL_HR, modeTip);
        menu.getNotSelectedItem(0)->configTip(T_TEXT_EXTERNAL_HR, modeTip);

        touchgfx::Unicode::UnicodeChar tip[16];
        devicesTip(mConfig, tip, 16);
        menu.getSelectedItem(1)->configTip(T_TEXT_DEVICES, tip);
        menu.getNotSelectedItem(1)->configTip(T_TEXT_DEVICES, tip);

        // No value on this row: it opens a screen.
        menu.getSelectedItem(2)->config(T_TEXT_COMPASS);
        menu.getNotSelectedItem(2)->config(T_TEXT_COMPASS);
        break;
    }
    case Mode::Devices: {
        // [0..count-1] remembered straps, [count] "Add new".
        menu.setNumberOfItems(static_cast<int16_t>(mConfig.count + 1));
        for (uint8_t i = 0; i < mConfig.count; ++i) {
            configName(i, mConfig.names[i]);
        }
        uint16_t addNew = mConfig.count;
        menu.getSelectedItem(addNew)->config(T_TEXT_ADD_NEW);
        menu.getNotSelectedItem(addNew)->config(T_TEXT_ADD_NEW);
        break;
    }
    default:
        // Scan and the result modes (ConfirmDelete/Deleted/Saved) render an
        // overlay (Searching... text or the result panel), not the menu list.
        break;
    }

    menu.invalidate();
}

void SettingsSensorsView::updateScan()
{
    if (mScan.empty()) {
        // Continuous discovery: animated "Searching..." centred text (no scroll
        // wheel, so no single-item wheel animation) — matches the design.
        showSearching();
    } else {
        // Found straps: switch to the scroll-wheel list.
        uint16_t sel = menu.getSelectedItem();
        hideResult();
        menu.setNumberOfItems(static_cast<int16_t>(mScan.size()));
        for (uint16_t i = 0; i < mScan.size(); ++i) {
            configName(i, mScan[i].name);
        }
        menu.invalidate();
        menu.snapToItem(sel);
    }
}

void SettingsSensorsView::enterScan()
{
    mScan.clear();
    // Start a fresh scan at the top: drop the stale Devices selection so the
    // first results list highlights row 0 rather than the previous index.
    menu.selectItem(0);
    presenter->accessoryScanStart();
    setMode(Mode::Scan);
}

void SettingsSensorsView::exitScan()
{
    presenter->accessoryScanStop();
    setMode(Mode::Devices);
}

void SettingsSensorsView::handleKeyEvent(uint8_t key)
{
    if (key == Gui::Config::Button::L1) {
        if (mMode == Mode::Sensors || mMode == Mode::Devices || mMode == Mode::Scan) {
            menu.selectPrev();
        }
        return;
    }

    if (key == Gui::Config::Button::L2) {
        if (mMode == Mode::Sensors || mMode == Mode::Devices || mMode == Mode::Scan) {
            menu.selectNext();
        }
        return;
    }

    if (key == Gui::Config::Button::R1) {
        uint16_t id = menu.getSelectedItem();

        switch (mMode) {
        case Mode::Sensors:
            if (id == 0) {
                // Cycle Off -> External & Wrist -> External Only -> Off.
                presenter->accessorySetMode(
                        static_cast<uint8_t>((mConfig.mode + 1) % 3));
            } else if (id == 1) {
                setMode(Mode::Devices);
            } else {
                application().gotoSettingsCompassScreenNoTransition();
            }
            break;
        case Mode::Devices:
            if (id < mConfig.count) {
                mDeleteIndex = static_cast<uint8_t>(id);
                setMode(Mode::ConfirmDelete);
            } else {
                enterScan();
            }
            break;
        case Mode::Scan:
            if (!mScan.empty() && id < mScan.size()) {
                std::snprintf(mResultName, sizeof(mResultName), "%s", mScan[id].name);
                presenter->accessoryRemember(mScan[id]);
                presenter->accessoryScanStop();
                mResultTicks = Gui::Config::kSavedScreenTimeout;
                setMode(Mode::Saved);
            }
            break;
        case Mode::ConfirmDelete:
            presenter->accessoryForget(mDeleteIndex);
            mResultTicks = Gui::Config::kSavedScreenTimeout;
            setMode(Mode::Deleted);
            break;
        case Mode::Deleted:
        case Mode::Saved:
            setMode(Mode::Devices);
            break;
        }
        return;
    }

    if (key == Gui::Config::Button::R2) {
        switch (mMode) {
        case Mode::Sensors:        application().gotoSettingsScreenNoTransition(); break;
        case Mode::Devices:        setMode(Mode::Sensors); break;
        case Mode::Scan:           exitScan();             break;
        case Mode::ConfirmDelete:  setMode(Mode::Devices); break;
        case Mode::Deleted:
        case Mode::Saved:          setMode(Mode::Devices); break;
        }
        return;
    }
}

void SettingsSensorsView::handleTickEvent()
{
    // Animate the "Searching..." dots (1 -> 2 -> 3) without moving the word.
    if (mMode == Mode::Scan && mScan.empty()) {
        if (++mDotTicks >= kDotPeriod) {
            mDotTicks = 0;
            mDots = static_cast<uint8_t>((mDots % 3) + 1);
            setSearchingText();
            resultText.invalidate();
        }
    }

    // Auto-dismiss the Deleted/Saved confirmation back to the Devices list.
    if (mMode == Mode::Deleted || mMode == Mode::Saved) {
        if (mResultTicks > 0 && --mResultTicks == 0) {
            setMode(Mode::Devices);
        }
    }
}
