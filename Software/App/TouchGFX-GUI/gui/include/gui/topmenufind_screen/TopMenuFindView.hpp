#ifndef TOPMENUFINDVIEW_HPP
#define TOPMENUFINDVIEW_HPP

#include <gui_generated/topmenufind_screen/TopMenuFindViewBase.hpp>
#include <gui/topmenufind_screen/TopMenuFindPresenter.hpp>

class TopMenuFindView : public TopMenuFindViewBase
{
public:
    TopMenuFindView();
    virtual ~TopMenuFindView() {}
    virtual void setupScreen();
    virtual void tearDownScreen();
protected:
    virtual void handleKeyEvent(uint8_t key) override;
};

#endif // TOPMENUFINDVIEW_HPP
