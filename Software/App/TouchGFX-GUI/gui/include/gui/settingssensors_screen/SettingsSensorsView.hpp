#ifndef SETTINGSSENSORSVIEW_HPP
#define SETTINGSSENSORSVIEW_HPP

#include <gui_generated/settingssensors_screen/SettingsSensorsViewBase.hpp>
#include <gui/settingssensors_screen/SettingsSensorsPresenter.hpp>

#include <vector>

#if !defined(SIMULATOR)
#include "GuiBackendInterface.hpp"
#else // defined(SIMULATOR)
#include "../../../../Libs/Header/Components/Shared/GuiBackendInterface.hpp"
#endif // defined(SIMULATOR)

class SettingsSensorsView : public SettingsSensorsViewBase
{
public:
    SettingsSensorsView();
    virtual ~SettingsSensorsView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();

    /// The row of the top list; the device lists are rebuilt on return, so
    /// it is the only selection worth remembering.
    uint16_t getSensorsRow();

    // Pushed from the presenter (model state).
    void showConfig(const B2GEvent::AccessoryConfig& cfg);
    void showScanResults(const std::vector<B2GEvent::AccessoryScanResult>& results);

protected:
    virtual void handleKeyEvent(uint8_t key) override;
    virtual void handleTickEvent() override;

private:
    // Settings ▸ Sensors flow. Config is
    // separate from device management: Sensors (External HR toggle + Devices
    // count) -> Devices (saved straps + Add New) -> Scan (continuous discovery),
    // with Delete?/Deleted/Saved confirmation states. No live link status here
    // (that lives on the pre-activity/in-workout indicator).
    enum class Mode {
        Sensors,        // External HR (On/Off) + Devices (count)
        Devices,        // saved straps + "Add new"
        Scan,           // continuous discovery: Searching... -> found list
        ConfirmDelete,  // "Delete?" for the selected saved strap
        Deleted,        // "Deleted" confirmation (auto-dismiss -> Devices)
        Saved,          // "Saved" confirmation (auto-dismiss -> Devices)
    };

    Mode                                       mMode { Mode::Sensors };
    B2GEvent::AccessoryConfig                  mConfig {};
    std::vector<B2GEvent::AccessoryScanResult> mScan;
    uint8_t                                    mDeleteIndex { 0 };  // strap pending delete
    uint16_t                                   mResultTicks { 0 };  // Deleted/Saved auto-dismiss
    char                                       mResultName[24] { }; // device for confirm/result
    uint8_t                                    mDots { 3 };         // "Searching" 1..3 dots
    uint16_t                                   mDotTicks { 0 };     // dot-animation timer

    void setMode(Mode mode);
    void rebuild();          // builds the menu list for the list modes
    void updateScan();       // Searching... (centered text) or the found-device list
    void configName(uint16_t id, const char* name);
    void enterScan();
    void exitScan();

    // Centered confirm/result panel (Delete?/Deleted/Saved): show the icon +
    // text overlay and hide the menu's list, or restore the list. `confirm`
    // shows the tick/cross button glyphs (the Delete? question).
    void showResult(BitmapId icon, bool confirm);
    void hideResult();

    // "Searching..." overlay: left-aligned + anchored so the word stays put
    // while the trailing dots animate (1..3); the dot count is driven by ticks.
    void showSearching();
    void setSearchingText();
};

#endif // SETTINGSSENSORSVIEW_HPP
