/**
 ******************************************************************************
 * @file    AppManager.hpp
 * @brief
 ******************************************************************************
 *
 ******************************************************************************
 */

#ifndef __APP_MANAGER_HPP
#define __APP_MANAGER_HPP

#include <cstdint>
#include <cstddef>
#include <memory>

#include "App/MessageManager.hpp"
#include "App/Application.hpp"
#include "App/IconPool.hpp"
#include "Interfaces/Components/IFileSystem.hpp"
#include "Interfaces/Components/IButtons.hpp"


namespace App
{

/**
 * @brief Central application manager on the kernel side.
 *
 * Singleton class that manages all application.
 */
class Manager : public App::LifeCycleCallback,
                public Interface::IButtons::Callback
{

public:

    static Manager& GetInstance()
    {
        static Manager sInstance;
        return sInstance;
    }

    void attachCallback(App::LifeCycleCallback *pCallback) { mLifeCycleCb = pCallback; }

    void scan();

    bool addApp(std::shared_ptr<Interface::IFile> file);
    bool addAlias(std::shared_ptr<Interface::IFile> file);
    bool addAppsToRegistry();

    Application* findByPID(uint32_t pid);

    /**
     * @brief   The Clockface application with the given app ID.
     * @note    The type is part of the match, so an ID left over from an app
     *          that is no longer a face resolves to nothing rather than to
     *          the wrong app.
     * @param   appId: uappID of the wanted face; 0 selects the built-in face.
     * @retval  Pointer to the application, nullptr when it is not installed.
     */
    Application* findWatchFace(uint64_t appId);

    /**
     * @brief   The watch face whose GUI is loaded, or nullptr if none is.
     * @details Allocates nothing, unlike findAllByType(): it is asked for on
     *          the way to making room when memory has run out.
     */
    Application* findLoadedWatchFace();

    std::vector<Application*> findAllByType(App::Type t);
    std::vector<Application*> findAllAutorun();
    std::vector<Application*> findAllActive();
    std::vector<Application*> findAll();

    /**
     * @brief   Read one of an app's icons into a caller-provided buffer.
     * @param   app: the app to read from.
     * @param   normal: true for the 60 px icon, false for the 30 px one.
     * @param   dest: where to put it; untouched on failure.
     * @param   destSize: its size. A file claiming a larger icon is refused
     *          rather than allowed to run off the end.
     * @retval  true if the icon was read whole.
     * @note    Reads into @p dest rather than allocating: this is on the icon
     *          path, which runs at up to ten sets a second while a menu
     *          scrolls, and nothing here should be asking the heap for
     *          kilobytes that often.
     */
    bool readIcon(const App::Application *app, bool normal, uint8_t *dest, uint32_t destSize);

    std::vector<App::Item> getAppList(App::Type t);

    /**
     * @brief   Read one app's icons from its file.
     * @param   appId: the app to read; 0 or unknown yields nullptr.
     * @retval  The pair of icons, or nullptr if there is no such app.
     * @note    Two file reads and ~4.5 KB every call -- nothing is cached here.
     *          Ask only for what is about to be drawn, and hold the result for
     *          as long as it is on screen.
     */
    App::IconRef getIconSet(uint64_t appId);

    /**
     * @brief   The ids of the installed apps of one type.
     * @note    Listed by the same rule as getAppList().
     */
    std::vector<uint64_t> getAppIds(App::Type t);

    std::vector<App::GlanceData> getGlancesData();

    void autoRun();

    void stopAll();

    void startGlances();
    void stopGlances();

    /**
     * @brief   Free the instance of every app that has nothing loaded and
     *          nothing pending. See Application::releaseIfIdle().
     * @note    Backend thread only.
     */
    void releaseIdleApps();

private:

    Manager();
    virtual ~Manager();

    // Prevent copying
    Manager(const Manager&) = delete;
    Manager& operator=(const Manager&) = delete;

    // Message lifecycle manager
    App::MessageManager mMessageManager;

    std::vector<App::Application> mApps;

    App::LifeCycleCallback *mLifeCycleCb;

    /// srvPID of the app that currently owns the single home-widget slot, or 0
    /// if none. Last-START-wins; only the owner's UPDATE/STOP take effect; the
    /// slot is cleared when the owner's service stops (onSrvStop).
    uint32_t mWidgetOwnerPid = 0;

    /// Serializes the widget slot: the ownership decision AND the forward to the
    /// Backend are done together under this lock, because the four widget hooks
    /// run on different apps' service threads. Held only across the (bounded)
    /// enqueue into the GUI event queue, so it never blocks unboundedly.
    OS::Mutex mWidgetMutex;

    /// GUI Frame tick timer
    OS::Timer<Manager> mGuiTickTimer;

    /// GLANCE Frame tick timer
    OS::Timer<Manager> mGlanceTickTimer;

    /// Guards every cross-thread deref of Application::mApp: the tick/button
    /// dispatch runs on the timer-daemon and DebounceDaemon threads while
    /// create()/destroy() run on the Backend thread. Shared by all apps.
    ///
    /// Taken with osWaitForever (OS::MutexCS) everywhere, which cannot hang
    /// because the hold time is always bounded and the lock is deadlock-free:
    ///  - Bounded hold: under the lock we only assign/swap the mApp pointer
    ///    (create/destroy) or run one dispatch -- a queue push capped at
    ///    DualAppComm::SEND_TIMEOUT_MS plus a non-blocking (timeout 0) pool
    ///    allocate. The heavy work (new/init in create, deinit/thread-join/
    ///    delete in destroy) is done OUTSIDE the lock, so no holder ever blocks
    ///    on an unbounded operation while holding it.
    ///  - No deadlock cycle: this is always the outermost lock. It is never
    ///    acquired while holding the message pool / queue / AppInstance::mMutex,
    ///    and the threads that drain those (the app's own GUI/service threads)
    ///    never take this one -- so there is no reverse ordering.
    ///  - Non-recursive-safe: dispatch calls the raw AppInstance methods, not
    ///    the lock-taking Application wrappers, so the lock is never re-entered.
    OS::Mutex mAppLifecycleMutex;


    bool appExists(const App::Info &info) const;

    /// Whether an app belongs in a list shown to the user: the file it was
    /// found in, and the code it runs, are both still there.
    static bool isListable(const App::Application &app);
    App::Application* contains(const App::Application* ptr);

    void guiTickTimCb(void *arg);
    void glanceTickTimCb(void *arg);

    bool isGuiTimerRequired();
    bool isGlanceTimerRequired();

    // LifeCycleCallback
    virtual void onSrvStart(App::Application *app) override;
    virtual void onSrvStop(App::Application *app) override;
    virtual void onGuiStart(App::Application *app) override;
    virtual void onGuiStop(App::Application *app) override;
    virtual void onIdle(App::Application *app) override;
    virtual void onGuiResume(App::Application *app) override;
    virtual void onGuiSuspend(App::Application *app) override;
    virtual void onGlanceRun(App::Application *app) override;
    virtual void onGlanceStop(App::Application *app) override;
    virtual void onGuiResumeRequest(App::Application *app) override;
    virtual void onGuiStartFailed(App::Application *app) override;
    virtual void onCapabilities(App::Application *app, const App::Capabilities &caps) override;
    virtual void onNewActivity(App::Application *app) override;
    virtual void onWidgetStart(App::Application *app) override;
    virtual void onWidgetUpdate(App::Application *app, const App::WidgetContent &content) override;
    virtual void onWidgetStop(App::Application *app) override;

    virtual void onPress(Interface::IButtons::Id id) override;
    virtual void onRelease(Interface::IButtons::Id id) override;
    virtual void onClick(Interface::IButtons::Id id) override;
    virtual void onLongPress(Interface::IButtons::Id id) override;
    virtual void onHold_1s(Interface::IButtons::Id id) override;
    virtual void onHold_5s(Interface::IButtons::Id id) override;
    virtual void onHold_10s(Interface::IButtons::Id id) override;

};

} // namespace App2

#endif // __APP_MANAGER_HPP
