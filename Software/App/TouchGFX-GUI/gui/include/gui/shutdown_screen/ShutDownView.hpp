#ifndef SHUTDOWNVIEW_HPP
#define SHUTDOWNVIEW_HPP

#include <gui_generated/shutdown_screen/ShutDownViewBase.hpp>
#include <gui/shutdown_screen/ShutDownPresenter.hpp>

class ShutDownView : public ShutDownViewBase
{
public:
    ShutDownView();
    virtual ~ShutDownView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
    uint16_t mCounter = Gui::Config::kShutdownScreenTimeout;
    bool mCommandSent {};
    virtual void handleTickEvent() override;
};

#endif // SHUTDOWNVIEW_HPP
                                                                                            