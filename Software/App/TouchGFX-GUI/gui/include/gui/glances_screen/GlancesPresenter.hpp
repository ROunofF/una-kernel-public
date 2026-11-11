#ifndef GLANCESPRESENTER_HPP
#define GLANCESPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class GlancesView;

class GlancesPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    GlancesPresenter(GlancesView& v);

    /**
     * The activate function is called automatically when this screen is "switched in"
     * (ie. made active). Initialization logic can be placed here.
     */
    virtual void activate();

    /**
     * The deactivate function is called automatically when this screen is "switched out"
     * (ie. made inactive). Teardown functionality can be placed here.
     */
    virtual void deactivate();

    virtual ~GlancesPresenter() {}

    virtual void onPhoneNotificationListChanged(const std::vector<std::shared_ptr<PhoneNotification>> &list) override;
    virtual void onAppGlanceList(const std::shared_ptr<std::vector<App::GlanceData>> &list) override;

    void requestGlanceList();

private:
    GlancesPresenter();

    GlancesView& view;
};

#endif // GLANCESPRESENTER_HPP
