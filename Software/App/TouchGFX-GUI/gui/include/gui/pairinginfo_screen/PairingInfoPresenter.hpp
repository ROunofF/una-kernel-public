#ifndef PAIRINGINFOPRESENTER_HPP
#define PAIRINGINFOPRESENTER_HPP

#include <gui/model/ModelListener.hpp>
#include <mvp/Presenter.hpp>

using namespace touchgfx;

class PairingInfoView;

class PairingInfoPresenter : public touchgfx::Presenter, public ModelListener
{
public:
    PairingInfoPresenter(PairingInfoView& v);

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

    virtual ~PairingInfoPresenter() {}

private:
    PairingInfoPresenter();

    PairingInfoView& view;
};

#endif // PAIRINGINFOPRESENTER_HPP
