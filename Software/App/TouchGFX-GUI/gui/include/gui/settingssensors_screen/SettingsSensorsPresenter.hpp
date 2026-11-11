#ifndef SETTINGSSENSORSPRESENTER_HPP
#define SETTINGSSENSORSPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

#if !defined(SIMULATOR)
#include "GuiBackendInterface.hpp"
#else // defined(SIMULATOR)
#include "../../../../Libs/Header/Components/Shared/GuiBackendInterface.hpp"
#endif // defined(SIMULATOR)

using namespace touchgfx;

class SettingsSensorsView;

class SettingsSensorsPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    SettingsSensorsPresenter(SettingsSensorsView& v);

    virtual void activate();
    virtual void deactivate();

    virtual ~SettingsSensorsPresenter() {}

    // No onIdleTimeout() override: this screen drives BLE scan/connect, which
    // the user must be able to wait on without the 30s menu timeout bouncing
    // back to the watchface. Inheriting ModelListener's no-op default means the
    // screen never auto-times-out — same as the Pairing / FirmwareUpdate
    // screens, which likewise wait for an async result. Exit is via R2 (back).

    // ModelListener — accessory state updates.
    virtual void onAccessoryConfig() override;
    virtual void onAccessoryScanResult() override;
    virtual void onAccessoryStatus() override;

    // Commands invoked by the view.
    void accessorySetMode(uint8_t mode);
    void accessoryScanStart();
    void accessoryScanStop();
    void accessoryForget(uint8_t index);
    void accessoryRemember(const B2GEvent::AccessoryScanResult& dev);

private:
    SettingsSensorsPresenter();

    SettingsSensorsView& view;
};

#endif // SETTINGSSENSORSPRESENTER_HPP
