#ifndef TOPMENUFINDPRESENTER_HPP
#define TOPMENUFINDPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class TopMenuFindView;

class TopMenuFindPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    TopMenuFindPresenter(TopMenuFindView& v);

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

    virtual ~TopMenuFindPresenter() {}
    virtual void onFindPhoneConfirmed() override;
    virtual void onAppStartReq(uint32_t pid) override {}  // Not called in this screen
    virtual void onAppLaunchFailed() override {}  // Not shown over this screen

private:
    TopMenuFindPresenter();

    TopMenuFindView& view;
};

#endif // TOPMENUFINDPRESENTER_HPP
