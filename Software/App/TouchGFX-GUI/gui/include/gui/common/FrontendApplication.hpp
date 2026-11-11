#ifndef FRONTENDAPPLICATION_HPP
#define FRONTENDAPPLICATION_HPP

#include <gui_generated/common/FrontendApplicationBase.hpp>

class FrontendHeap;

using namespace touchgfx;

class FrontendApplication : public FrontendApplicationBase
{
public:
    FrontendApplication(Model& m, FrontendHeap& heap);
    virtual ~FrontendApplication() { }

    virtual void handleTickEvent()
    {
        model.tick();
        FrontendApplicationBase::handleTickEvent();
    }

    // Get keys event in Model to simulate backend actions 
    void handleKeyEvent(uint8_t key)
    {
        model.handleKeyEvent(key);
        FrontendApplicationBase::handleKeyEvent(key);
    }

private:
};

#endif // FRONTENDAPPLICATION_HPP
