#ifndef SPLASH1VIEW_HPP
#define SPLASH1VIEW_HPP

#include <gui_generated/splash1_screen/Splash1ViewBase.hpp>
#include <gui/splash1_screen/Splash1Presenter.hpp>

class Splash1View : public Splash1ViewBase
{
public:
    Splash1View();
    virtual ~Splash1View() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // SPLASH1VIEW_HPP
