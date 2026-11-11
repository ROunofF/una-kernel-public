/**
 * @file TestAppManager.cpp
 * @brief Comprehensive tests for App2::Application with real FreeRTOS threads
 */

#include "Test/TestAppManager.hpp"

#include "App/Manager.hpp"


#define LOG_MODULE_PRX      "TestAppManager"
#define LOG_MODULE_LEVEL    LOG_LEVEL_DEBUG
#include "Logger.h"


void TestAppManager_execute()
{
    LOG_DEBUG("Scan apps\n");
    App::Manager &appmgr = App::Manager::GetInstance();
    appmgr.scan();
    std::vector<App::Application*> apps = appmgr.findAll();
//    for (auto &app : apps) {
//        LOG_DEBUG("App: %s\n", app->getName());
//        app->run(0);
//        app->stop(0);
//        app->run(0);
//        app->stop(0);
//    }

    LOG_DEBUG("Run\n======================\n");
    apps[0]->run(0);
    //OS::Delay(1000);

    LOG_DEBUG("Stop\n======================\n");
    apps[0]->stop(0);
    //OS::Delay(10);


    LOG_DEBUG("Run\n======================\n");
    apps[0]->run(0);

    OS::Delay(5000);
    apps[0]->resume();

    OS::Delay(20000);
    apps[0]->suspend();

    OS::Delay(5000);
    apps[0]->stop(0);

    while(1) {
        OS::Delay(100);
    }

}
