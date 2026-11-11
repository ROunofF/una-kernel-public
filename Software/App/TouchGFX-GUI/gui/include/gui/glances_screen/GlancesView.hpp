#ifndef GLANCESVIEW_HPP
#define GLANCESVIEW_HPP

#include <gui_generated/glances_screen/GlancesViewBase.hpp>
#include <gui/glances_screen/GlancesPresenter.hpp>

class GlancesView : public GlancesViewBase
{
public:
    GlancesView();
    virtual ~GlancesView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();

    void setPositionId(uint16_t id);
    uint16_t getPositionId();

    void setNotifications(uint32_t callsNum, uint32_t emailsNum, uint32_t msgsNum);
    void updAppList(const std::shared_ptr<std::vector<App::GlanceData>> &list);
protected:
    virtual void handleKeyEvent(uint8_t key) override;

    uint16_t mCounter = Gui::Config::kGlanceScreenTimeout;
    uint16_t mGlanceUpdCounter = GUI_CONFIG_MS_2_TICKS(950); // first update will be in 50ms
    virtual void handleTickEvent() override;
};

#endif // GLANCESVIEW_HPP
