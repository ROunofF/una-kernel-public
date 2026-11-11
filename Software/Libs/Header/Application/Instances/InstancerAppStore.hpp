/**
 *******************************************************************************
 * @file    InstanceUserAppLoader.hpp
 * @brief   Initialization of the UserAppLoader
 * 
 *******************************************************************************
 *
 *******************************************************************************
 */

#pragma once

#include "Porting.h"
#include "UserAppLayer/UserAppManager.hpp"

namespace Instance {

    class AppStore {
    public:
        static AppStore& getInstance();

        AppStore(const AppStore& other)            = delete;
        AppStore& operator=(const AppStore& other) = delete;

        AppStore(AppStore&& other)                 = delete;
        AppStore& operator=(AppStore&& other)      = delete;

        App::Manager& getAppManager();

    private:
        class Store : public App::IStore
        {
        public:
            Store();

            void addFile(Interface::IFile* f);

            void scan(std::vector<App::Info>& list) override;
            std::shared_ptr<uint8_t> loadIcon(const std::string& location, bool normal = true) override;

        private:
            void scan(Interface::IFile& f, App::Info& info);

            std::vector<Interface::IFile*> mFiles;
        };

        AppStore();

        void scanFileSystem(Store& store);

        Store         mStore;
        App::Manager& mAppManager;
    };

}

