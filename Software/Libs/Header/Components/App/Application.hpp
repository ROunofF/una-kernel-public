
#ifndef __APP_APPLICATION_HPP
#define __APP_APPLICATION_HPP

#include <cstdint>
#include <atomic>
#include <cstddef>
#include <memory>


#include "OS.hpp"
#include "Interfaces/Components/IButtons.hpp"
#include "Interfaces/Components/IFileSystem.hpp"
#include "FileSystem/FileSystemGuard.hpp"
#include "AppSurface.hpp"
#include "Shared/AppLaunchFailure.hpp"

#include "SDK/Messages/MessageBase.hpp"
#include "SDK/Messages/CommandMessages.hpp"


#include "App/MessageManager.hpp"
#include "App/Kernel.hpp"
#include "App/Loader.hpp"
#include "App/DualAppComm.hpp"
#include "App/FileSystem.hpp"
#include "App/SensorListener.hpp"
#include "App/ActivityFileRegistry.hpp"

// Forward declaration
namespace App
{
  class Manager;
}

namespace App
{

struct Info {
    uint64_t appID = 0;
    uint32_t version;

    uint32_t srvPID = 0;
    uint32_t guiPID = 0;

    uint32_t srvSize = 0;
    uint32_t guiSize = 0;

    uint32_t flags = 0;

    char name[App::kNameLen] {};

    /// Identity: the file found in the app's directory (real app or variant
    /// alias). Drives the sandbox root, PIDs, icons, ghost-check and the
    /// registry path.
    std::shared_ptr<Interface::IFile> file;

    /// Where the code blobs are loaded from: equal to `file` for real apps,
    /// the target app's .uapp for variants. Always its own IFile instance,
    /// never a handle shared with another entry.
    std::shared_ptr<Interface::IFile> codeFile;

    /// True when this entry is a code-less variant alias.
    bool isVariant = false;

    /// The target app's uappID (variants only; 0 otherwise). Registry use.
    uint64_t targetAppID = 0;
};

class IGlance {
public:
    virtual ~IGlance() = default;

    virtual App::GlanceData glanceGetInfo() = 0;
    virtual void glanceUpdate()  = 0;
    virtual void glanceClose()   = 0;
};

/// Sanitized content of the home-screen widget an app pushes via
/// REQUEST_WIDGET_UPDATE: shown masked to known bits, percent clamped to 0..100,
/// text bounded and NUL-terminated (valid UTF-8 only if the sender kept the
/// contract -- an unterminated full-buffer sender may leave a split trailing
/// byte, still bounded and safe).
struct WidgetContent {
    uint32_t shown   = 0;                               // SDK WidgetShow bitmask
    float    percent = 0.0f;                            // 0..100
    char     text[SDK::Message::WIDGET_TEXT_BYTES] {};  // label, UTF-8
};


class Application;

/**
 * @brief   App Life Cycle Callbacks
 */
class LifeCycleCallback {
public:

    /**
     * @brief   Called when the Service started.
     * @param   app: pointer to the Application.
     */
    virtual void onSrvStart(App::Application *app) {}

    /**
     * @brief   Called when the Service stopped.
     * @param   app: pointer to the Application.
     */
    virtual void onSrvStop(App::Application *app) {}

    /**
     * @brief   Called when the GUI started.
     * @param   app: pointer to the Application.
     */
    virtual void onGuiStart(App::Application *app) {}

    /**
     * @brief   Called when the GUI stopped.
     * @param   app: pointer to the Application.
     */
    virtual void onGuiStop(App::Application *app) {}

    /**
     * @brief   Called when neither process is loaded and no request is pending.
     * @details Reported from the app's own thread, which cannot be the one to
     *          free the instance: freeing it joins that thread. A receiver
     *          that acts on it must do so from the thread that sends the
     *          app its requests, and check again there with releaseIfIdle().
     * @param   app: pointer to the Application.
     */
    virtual void onIdle(App::Application *app) {}

    /**
     * @brief   Called when the GUI resumed.
     * @param   app: pointer to the Application.
     */
    virtual void onGuiResume(App::Application *app) {}

    /**
     * @brief   Called when the GUI paused.
     * @param   app: pointer to the Application.
     */
    virtual void onGuiSuspend(App::Application *app) {}

    /**
     * @brief   Called when the Glance mode activated.
     * @param   app: pointer to the Application.
     */
    virtual void onGlanceRun(App::Application *app) {}

    /**
     * @brief   Called when the Glance mode deactivated.
     * @param   app: pointer to the Application.
     */
    virtual void onGlanceStop(App::Application *app) {}

    /**
     * @brief   Called when the APP wants to wake up GUI.
     * @note    Backend must manually resume the application
     *          by calling the resume() method.
     * @param   app: pointer to the Application.
     */
    virtual void onGuiResumeRequest(App::Application *app) {}

    /**
     * @brief   Called when a service's own request for its GUI was refused
     *          for want of memory.
     * @details The service keeps running and has been told the request
     *          failed. A receiver that can make room may start the GUI for it
     *          with Application::runGuiForService(). Called from the app's own
     *          thread, so the room has to be made from the Backend thread.
     * @param   app: pointer to the Application.
     */
    virtual void onGuiStartFailed(App::Application *app) {}

    /**
     * @brief   Called when the GUI capabilities changed.
     * @param   app: pointer to the Application.
     * @param   caps: APP capabilities.
     */
    virtual void onCapabilities(App::Application *app, const App::Capabilities &caps) {};

    /**
     * @brief   Called after saving the activity file.
     * @param   app: pointer to the Application.
     */
    virtual void onNewActivity(App::Application *app) {};

    /**
     * @brief   Called when an app claims the home-screen widget slot.
     * @param   app: pointer to the Application.
     */
    virtual void onWidgetStart(App::Application *app) {}

    /**
     * @brief   Called when the widget owner pushes new content.
     * @param   app: pointer to the Application.
     * @param   content: sanitized label/percent to show.
     */
    virtual void onWidgetUpdate(App::Application *app, const App::WidgetContent &content) {}

    /**
     * @brief   Called when the widget owner releases the slot.
     * @param   app: pointer to the Application.
     */
    virtual void onWidgetStop(App::Application *app) {}

protected:

    /**
     * @brief   Destructor.
     */
    virtual ~LifeCycleCallback() = default;
};

class AppInstance {
public:
    AppInstance(App::Info&           info,
                App::MessageManager& messageMgr,
                LifeCycleCallback*   cb,
                Application*         base,
                AppSurface&          surface);

    virtual ~AppInstance();

    bool init();
    bool deinit();
    bool isInitialized() const;

    bool run(uint32_t timeout = 0);
    bool runService(uint32_t timeout = 0);
    bool stop(uint32_t timeout = 0);

    /**
     * @brief   Why the last run() failed.
     * @return  The reason, or LaunchFailure::NONE if it succeeded. Only
     *          meaningful straight after run() returns false: run() blocks
     *          until the app answers or the timeout expires, so the value is
     *          settled by the time it returns, but a later launch overwrites
     *          it. The launcher uses this to choose which message to show.
     */
    LaunchFailure getLaunchFailure() const { return mLaunchFailure; }
    bool suspend(uint32_t timeout = 0);
    bool resume(uint32_t timeout = 0);
    bool runGlance(uint32_t timeout = 0);
    bool stopGlance(uint32_t timeout = 0);
    bool runGuiForService(uint32_t timeout = 0);

    bool isSrvRun() const { return mSrvState >= AppPartState::RUN; }
    bool isGuiRun() const { return mGuiState >= AppPartState::RUN; }
    bool isGuiResumed() const { return mGuiState == AppPartState::RESUMED; }
    bool isGlanceRun() const { return mSrvState == AppPartState::GLANCE; }

    /**
     * @brief   Whether a lifecycle request is still queued or being handled.
     */
    bool hasPendingRequests() const { return mRequestsInFlight.load() != 0; }

    /**
     * @brief   Whether the instance can be freed.
     * @retval  'true' if neither process is loaded and every request sent to
     *          the app thread has been handled. Loaded, not running: a
     *          process told to stop reads as stopped at once but stays loaded
     *          until it has acknowledged, and freeing the instance before then
     *          would unload it in the middle of its stop.
     */
    bool isIdle() const;

    App::Capabilities getCapabilities();
    App::GlanceData getGlanceData();

    void onFrameTick(uint32_t timestamp);


    void onGlanceTick(uint32_t timestamp);
    void onGlanceStop();

    void onEventButton(uint32_t timestamp, SDK::Message::EventButton::Id id, SDK::Message::EventButton::Event event);

    // Temporary for testing
    App::DualAppComm& getComm() {return mAppComm;};

protected:
    App::Info &mInfo;

    /// Application name for Service part
    char mSrvName[32];

    /// Application name for GUI part
    char mGuiName[32];

    App::MessageManager &mMessageMgr;
    App::DualAppComm mAppComm;

    // One common file storage for both parts of application
    FileSystemGuard     mUserFs;
    App::FileSystem     mFsAPI;


    // Kernel
    App::Kernel mSrvKernel;
    App::Kernel mGuiKernel;

    // Loaders
    App::Loader mSrvLoader;
    App::Loader mGuiLoader;

    App::LifeCycleCallback *mLifeCycleCb;
    App::Application *mBase;

    // Where an app's frames go. Typed as the surface rather than as a display,
    // because a frame is only taken from the app that holds it -- see
    // AppSurface::write().
    AppSurface &mSurface;

    App::Capabilities mCapabilities;
    App::GlanceData mGlanceData;


    /// Indicates that the app is initialized
    enum class InitState { None, InProgress, Initialized};
    std::atomic<InitState> mInitState{InitState::None};

    OS::Mutex mMutex;
    struct Flag {
          static constexpr uint32_t Stopped      = (1 << 3);
          static constexpr uint32_t GuiIsActive  = (1 << 4);
    };

    // Bounded stop waits. By contract a live app acknowledges a stop request
    // promptly (Kernel::exit() -> REQUEST_APP_TERMINATE -> Flag::Stopped); a
    // service may otherwise idle on its event queue forever by design, so we
    // never wait unbounded. skStopGraceMs must comfortably exceed one bounded
    // kernel op (e.g. a filesystem flush) so a slow-but-live app is not
    // mistaken for a wedged one; on expiry the app is force-terminated.
    static constexpr uint32_t skStopGraceMs = 10000;
    static constexpr uint32_t skStopForceMs = 1000;

    OS::EventFlags mFlags;
    OS::Thread<App::AppInstance, 4*1024> mThread;

    uint32_t mGuiTickNumber;
    uint32_t mGlanceTickNumber;
    uint32_t mStartGuiTime;

    enum class AppPartState {
        NONE,
        RUN,
        RESUMED, // Only for GUI
        GLANCE,  // Only for Service
    };


    AppPartState mSrvState {AppPartState::NONE};
    AppPartState mGuiState {AppPartState::NONE};

    /// Why the last run() failed, for the message the launcher shows.
    ///
    /// Atomic because the two ends genuinely can overlap. run() sets a reason
    /// up front and the app's own thread overwrites it from runSrv/runGui -
    /// but on the timeout path run() returns while that thread is still
    /// inside them, and the caller reads immediately. The read is then racing
    /// a write. Either value is a truthful answer, so this is about defining
    /// the race rather than winning it.
    std::atomic<LaunchFailure> mLaunchFailure {LaunchFailure::NONE};

    /// Lifecycle requests queued for the app thread and not yet handled.
    ///
    /// Nothing being loaded does not on its own mean the instance is free to
    /// go: most requests are sent without waiting, so a launch can be sitting
    /// in the queue with nothing loaded yet. Raised before the push and
    /// lowered after the handler has run and mProcessesLoaded is published, so
    /// a reader that sees zero also sees what the last handler left loaded.
    /// Requests dropped by a teardown are never lowered, which does not
    /// matter: the instance is being freed.
    std::atomic<uint32_t> mRequestsInFlight {0};

    /// Whether either process is loaded, as of the last message the app
    /// thread handled. Published for the Backend thread, which cannot read the
    /// loaders themselves: they change only on the app thread. Every load
    /// happens inside a counted request or while the service is already
    /// loaded, so this never reads false while a load is unpublished.
    std::atomic<bool> mProcessesLoaded {false};

    App::SensorListener mSrvSensorListener;
    App::SensorListener mGuiSensorListener;

    void thread();

    bool sendToAppAndRelease(SDK::MessageBase* msg, uint32_t timeout);
    bool sendToAppQueue(SDK::MessageBase* msg, uint32_t timeout = 100);

    bool loadService();
    bool loadGui();


    bool runSrv();
    bool stopSrv();
    bool runGui();
    bool stopGui();
    bool resumeGui();
    bool suspendGui();
    bool runGlc();
    bool stopGlc();

    // Kernel-side force-terminate of a wedged app that ignored the graceful
    // stop request. Injects a service-PID REQUEST_APP_TERMINATE into the common
    // queue so the dispatch thread runs the full teardown (unload -> thread
    // terminate) and sets Flag::Stopped, mirroring the GUI-unresponsive path.
    void forceTerminateService();

    void appLifeCycle(SDK::MessageBase* msg);

    /// Start the GUI for a running service and bring it on screen: the body of
    /// both the service's own REQUEST_APP_RUN_GUI and the kernel's
    /// APP_RUN_GUI. Only the service's request reports an out-of-memory
    /// refusal, so the kernel's retry cannot trigger another.
    void handleRunGui(SDK::MessageBase* msg, bool requestedByService);
    void appMsgHandler(SDK::MessageBase* msg);
    void slMsgHandler(SDK::MessageBase* msg);
};

class Application : public IFileEventListener {
public:
    Application(const App::Info &info,
            App::MessageManager &messageMgr,
            LifeCycleCallback *cb,
            AppSurface &surface,
            OS::Mutex &lifecycleMutex);

    virtual ~Application()
    {
        if (isCreated()) {
            destroy();
        }
    }

    const App::Info& getInfo() const {return mInfo;};
    uint64_t getAppID() const {return mInfo.appID;}
    uint32_t getSrvPID() const {return mInfo.srvPID;}
    uint32_t getGuiPID() const {return mInfo.guiPID;}
    const char* getName() const {return mInfo.name;}
    App::Capabilities getCapabilities();
    App::GlanceData getGlanceData();

    /**
     * @brief   Base type of the application.
     * @details Classification, not capability: exactly one of the four values
     *          the type mask holds, and nothing else in the flags word takes
     *          part. A Clockface that also carries the glance-interface flag
     *          is a Clockface. Use it where a single value is recorded or
     *          reported; ask isType() whether an app fills a given role.
     * @retval  The base type held in the app's flags.
     */
    App::Type getType() const;

    /**
     * @brief   Whether the application fills a given role.
     * @details The predicate form, for the queries that take a type at run time
     *          and want every app matching it. A role is filled by the base
     *          type alone: the format's one interface flag,
     *          FLAG_APP_HAS_GLANCE_INTF, is not read anywhere in the kernel
     *          (its definition says why), so no app answers to a type other
     *          than its own. Honouring an interface would widen this and leave
     *          getType() untouched.
     * @param   t: role to test.
     * @retval  'true' if the app fills the role, 'false' otherwise.
     */
    bool isType(App::Type t) const;

    bool isAutorun() const;

    bool run(uint32_t timeout = 0);
    bool runService(uint32_t timeout = 0);  // for autorun
    bool stop(uint32_t timeout = 0);

    /**
     * @brief   Why the last run() failed.
     * @return  The reason, or LaunchFailure::NONE if it succeeded. Only
     *          meaningful straight after run() returns false: run() blocks
     *          until the app answers or the timeout expires, so the value is
     *          settled by the time it returns, but a later launch overwrites
     *          it. The launcher uses this to choose which message to show.
     */
    LaunchFailure getLaunchFailure() const;
    bool suspend(uint32_t timeout = 0);
    bool resume(uint32_t timeout = 0);
    bool runGlance(uint32_t timeout = 0);
    bool stopGlance(uint32_t timeout = 0);

    /**
     * @brief   Start the GUI of an app whose service is already running, as
     *          if the service had asked for it.
     * @details For a service's own request that did not fit (see
     *          LifeCycleCallback::onGuiStartFailed()), once room has been
     *          made. Brought on screen exactly as the service's request would
     *          have been. A failure here is not reported back, so a retry
     *          cannot ask for another one.
     * @retval  'true' if the GUI started.
     */
    bool runGuiForService(uint32_t timeout = 0);

    /**
     * @brief   Whether a request sent to the app is still queued or being
     *          handled - one whose sender gave up waiting, for instance.
     * @retval  'false' if there is no instance.
     */
    bool hasPendingRequests() const;

    bool isActive() const { return isCreated(); }

    /**
     * @brief   Free the instance if the app has nothing loaded and nothing
     *          pending.
     * @details Without this an instance lives from the app's first use until
     *          shutdown, holding its thread and queues on the kernel heap for
     *          every app ever opened. Call from the Backend thread, the one
     *          that sends every request: that is what keeps the app idle
     *          between the check and the free.
     * @retval  'true' if an instance was freed.
     */
    bool releaseIfIdle();

    /**
     * @brief   Wait until the app has nothing loaded and nothing pending.
     * @details For relaunching an app whose launch just failed: the failure
     *          only tells its service to stop, and the service stays loaded
     *          until it has acknowledged. Launching again before then would
     *          load it a second time over itself. Call from the Backend
     *          thread, for the same reason as releaseIfIdle().
     * @param   timeoutMs: How long to wait.
     * @retval  'true' if the app was idle (or had no instance) in time.
     */
    bool waitUntilIdle(uint32_t timeoutMs);

    bool isSrvRun() const;
    bool isGuiRun() const;
    bool isGuiResumed() const;
    bool isGlanceRun() const;

    // Temporary for testing
    bool create();

    // Shared message pool (Manager-owned, outlives mApp): kernel-side senders
    // allocate/release from it on any thread, independently of app lifecycle.
    App::MessageManager& getMessageManager() { return mMessageMgr; }

    // Deliver a kernel-built message to the app's service queue. Takes the
    // lifecycle lock and returns false (dropping nothing of ours -- the caller
    // still owns and releases msg) when the app is not instantiated, so the
    // app's internal queues are never touched after a destroy(). Exposed as a
    // scoped operation rather than a raw DualAppComm accessor precisely so the
    // comm pointer cannot escape the lock and be dereferenced post-destroy().
    bool sendToService(SDK::MessageBase* msg);

private:
    App::Info mInfo;
    App::MessageManager &mMessageMgr;
    App::AppInstance* mApp;

    /// Why create() last failed, for when there is no instance to ask. The
    /// two ways it fails are not the same thing to a user: the allocation
    /// is out of memory, init() timing out is not.
    App::LaunchFailure mCreateFailure {App::LaunchFailure::NONE};
    App::LifeCycleCallback *mLifeCycleCb;
    AppSurface &mSurface;
    ActivityFileRegistry mActivityFileRegistry;
    char mActivityFilePrefix[FS::FileSystem::skMaxPathLen];
    bool mHasNewActivity;

    // Serializes create()/destroy() (Backend thread) against the tick/button
    // dispatch that derefs mApp from the timer-daemon and DebounceDaemon
    // threads. Owned by the Manager (stable singleton), shared by all apps.
    OS::Mutex *mpLifecycleMutex;

    //bool create();
    bool destroy();
    bool isCreated() const { return mApp != nullptr; }

    // Manager-side tick/button dispatch. Each takes *mpLifecycleMutex so the
    // mApp null-check and the deref are one atomic step with respect to
    // destroy(), closing the use-after-free window on the raw instance pointer.
    void dispatchFrameTick(uint32_t timestamp);
    void dispatchGlanceTick(uint32_t timestamp);
    void dispatchButton(uint32_t timestamp,
                        SDK::Message::EventButton::Id id,
                        SDK::Message::EventButton::Event event);

    friend ::App::Manager;

    virtual void onFileAppeared(const char *relativePath) override;
    virtual void onFileDisappeared(const char *relativePath) override;
    static bool ExtractAppPrefix(char *buf, size_t bufSize, const char *appFilePath);

    /**
     * @brief   Returns true if the relative path should be tracked:
     *          must start with /Activity/ and have a .fit extension.
     * @param   relativePath: Relative path to check.
     * @retval  'true' if the path matches the filter, 'false' otherwise.
     */
    static bool IsTrackedFile(const char *relativePath);

};



} // namespace App

#endif // __APP_APPLICATION_HPP
