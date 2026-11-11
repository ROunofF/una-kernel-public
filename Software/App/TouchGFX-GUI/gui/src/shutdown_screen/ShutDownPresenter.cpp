#include <gui/shutdown_screen/ShutDownView.hpp>
#include <gui/shutdown_screen/ShutDownPresenter.hpp>

ShutDownPresenter::ShutDownPresenter(ShutDownView& v)
    : view(v)
{

}

void ShutDownPresenter::activate()
{

}

void ShutDownPresenter::deactivate()
{

}

void ShutDownPresenter::shutdown()
{
    model->shutdown();
}