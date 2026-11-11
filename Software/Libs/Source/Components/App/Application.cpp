
#include "App/Application.hpp"
#include "Porting.h"

#define LOG_MODULE_PRX      "Application"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"

#include "App/InternalKernelMessages.hpp"


#include "FileSystem.hpp"
#include "AppTypes.hpp"
#include "ComponentFactory.hpp"
#include "SDK/Messages/SensorLayerMessages.hpp"
#include "SDK/Messages/AccessoryMessages.hpp"
#include "SensorLayer/Core/SensorManager.hpp"
#include "App/AccessoryManager.hpp"
#include "Buzzer.hpp"
#include "Vibro.hpp"
#include "Components/Settings/SettingsManager.hpp"
#include "Components/Settings/SettingsLocalManager.hpp"

#include "PathUtils.hpp"

namespace App
{

// App flag definitions live in App/AppHeaders.hpp (included via Application.hpp).

namespace {

/// The requests the kernel queues for an app thread, as opposed to what the
/// app's own processes send it. Only these are counted in mRequestsInFlight,
/// so the sender and the app thread have to agree on the set -- both ask here.
bool IsLifecycleRequest(SDK::MessageType::Type type)
{
    switch (type) {
        case InternalMessageType::APP_RUN_SRV:
        case InternalMessageType::APP_RUN_SRV_ANG_GUI:
        case InternalMessageType::APP_STOP_SRV_ANG_GUI:
        case InternalMessageType::APP_SUSPEND_GUI:
        case InternalMessageType::APP_RESUME_GUI:
        case InternalMessageType::APP_GLANCE_RUN:
        case InternalMessageType::APP_GLANCE_STOP:
        case InternalMessageType::APP_RUN_GUI:
            return true;
        default:
            return false;
    }
}

} // namespace

Application::Application(const App::Info &info,
            App::MessageManager &messageMgr,
            LifeCycleCallback *cb,
            AppSurface &surface,
            OS::Mutex &lifecycleMutex)
            : mInfo(info)
            , mMessageMgr(messageMgr)
            , mApp(nullptr)
            , mLifeCycleCb(cb)
            , mSurface(surface)
            , mActivityFileRegistry(FS::FileSystem::GetInstance(), APPS_ACTIVITY_FILE_REG_PATH)
            , mHasNewActivity(false)
            , mpLifecycleMutex(&lifecycleMutex)
{
    // Invariant: code always loads via codeFile, so any Info built without
    // one (every non-variant path) falls back to the app's own file here.
    if (!mInfo.codeFile) {
        mInfo.codeFile = mInfo.file;
    }

    const char *filePath = mInfo.file->getPath();
    if (!ExtractAppPrefix(mActivityFilePrefix, sizeof(mActivityFilePrefix), filePath)) {
        LOG_ERROR("Failed to extract app prefix from path: %s\n", filePath);
    }
}

App::Capabilities Application::getCapabilities()
{
    OS::MutexCS cs(*mpLifecycleMutex);
    if (mApp == nullptr) {
        return {};
    }

    return mApp->getCapabilities();
}

App::GlanceData Application::getGlanceData()
{
    OS::MutexCS cs(*mpLifecycleMutex);
    if (mApp == nullptr) {
        return {};
    }

    return mApp->getGlanceData();
}

App::Type Application::getType() const
{
    return App::TypeFromFlags(mInfo.flags);
}

bool Application::isType(App::Type t) const
{
    // The base type is the only thing that satisfies a role today. The one
    // interface flag the format has, FLAG_APP_HAS_GLANCE_INTF, is not read
    // anywhere in the kernel -- see its definition in AppHeaders.hpp for why.
    return t == getType();
}

bool Application::isAutorun() const
{
    return (mInfo.flags & FLAG_APP_AUTO_START);
}

bool Application::run(uint32_t timeout)
{
    if(!create()) {
        return false;
    }

    LOG_DEBUG("called\n");
    return mApp->run(timeout);
}

App::LaunchFailure Application::getLaunchFailure() const
{
    // Under the lifecycle mutex like every other reader of mApp: destroy()
    // swaps it under the same lock, and this runs on the Backend thread just
    // after run() returned, so an unlocked read is a use-after-free waiting
    // for the right interleaving.
    OS::MutexCS cs(*mpLifecycleMutex);

    // No instance means create() failed before anything could be loaded, so
    // the reason it recorded is the only one there is.
    return mApp ? mApp->getLaunchFailure() : mCreateFailure;
}

bool Application::runService(uint32_t timeout)
{
    if(!create()) {
        return false;
    }

    return mApp->runService(timeout);
}

bool Application::stop(uint32_t timeout)
{
    if (!isCreated()) {
        return true;
    }
    LOG_DEBUG("called\n");
    bool success = mApp->stop(timeout);

    if (timeout > 0) {
        return destroy();
    }

    if (mHasNewActivity) {
        mHasNewActivity = false;
        if (mLifeCycleCb) {
            mLifeCycleCb->onNewActivity(this);
        }
    }
    return success;
}

bool Application::suspend(uint32_t timeout)
{
    if (!isCreated()) {
        return false;
    }

    return mApp->suspend(timeout);
}

bool Application::resume(uint32_t timeout)
{
    if (!isCreated()) {
        return false;
    }

    return mApp->resume(timeout);
}

bool Application::runGlance(uint32_t timeout)
{
    if(!create()) {
        return false;
    }

    return mApp->runGlance(timeout);
}

bool Application::runGuiForService(uint32_t timeout)
{
    // Never creates an instance: there is only a GUI to start for a service
    // that is already running.
    if (!isCreated()) {
        return false;
    }
    return mApp->runGuiForService(timeout);
}

bool Application::hasPendingRequests() const
{
    OS::MutexCS cs(*mpLifecycleMutex);
    return mApp != nullptr && mApp->hasPendingRequests();
}

bool Application::stopGlance(uint32_t timeout)
{
    if (!isCreated()) {
        return true;
    }
    return mApp->stopGlance(timeout);
}

bool Application::isSrvRun() const
{
    // Lock so the null-check and the deref cannot straddle a concurrent
    // destroy() on the Backend thread (see the mpLifecycleMutex note).
    OS::MutexCS cs(*mpLifecycleMutex);
    return mApp != nullptr && mApp->isSrvRun();
}

bool Application::isGuiRun() const
{
    OS::MutexCS cs(*mpLifecycleMutex);
    return mApp != nullptr && mApp->isGuiRun();
}

bool Application::isGuiResumed() const
{
    OS::MutexCS cs(*mpLifecycleMutex);
    return mApp != nullptr && mApp->isGuiResumed();
}

bool Application::isGlanceRun() const
{
    OS::MutexCS cs(*mpLifecycleMutex);
    return mApp != nullptr && mApp->isGlanceRun();
}

bool Application::create()
{
    if (mApp != nullptr) {
        return true;
    }

    AppInstance *inst = new (std::nothrow) AppInstance(mInfo,
                                                       mMessageMgr,
                                                       mLifeCycleCb,
                                                       this,
                                                       mSurface);
    if (inst == nullptr) {
        mCreateFailure = LaunchFailure::OUT_OF_MEMORY;
        return false;
    }

    if (!inst->init()) {
        // Init failed: delete the instance and leave mApp null so the app stays
        // uncreated. mApp must never point at a freed instance -- a later
        // run()/deref of a dangling mApp would be a use-after-free.
        delete inst;
        // init() fails on a thread-init timeout, not on memory. Reporting
        // this as out of memory would send the user looking for space they
        // already have.
        mCreateFailure = LaunchFailure::TIMEOUT;
        return false;
    }

    // mApp is published only after the instance is fully initialized, and under
    // the lifecycle lock, so a concurrent tick/button dispatch either sees null
    // or a ready instance -- never a half-built one.
    {
        OS::MutexCS cs(*mpLifecycleMutex);
        mApp = inst;
    }

    LOG_DEBUG("Created\n");
    return true;
}

bool Application::destroy()
{
    // Detach mApp under the lock, then delete outside it. Once the locked swap
    // completes, mApp is unreachable to every reader (each dispatch/query takes
    // the same lock and re-reads mApp), so no other thread can still be inside
    // a call on `inst`. deinit() joins the app thread (up to 5 s), which must
    // NOT run under the lock or it would stall the timer-daemon / DebounceDaemon
    // dispatch that briefly contends for it.
    AppInstance *inst = nullptr;
    {
        OS::MutexCS cs(*mpLifecycleMutex);
        inst = mApp;
        mApp = nullptr;
    }

    if (inst != nullptr) {
        if (inst->isInitialized()) {
            inst->deinit();
        }
        delete inst;
        LOG_DEBUG("Destroyed\n");
    }

    return true;
}

bool Application::releaseIfIdle()
{
    {
        OS::MutexCS cs(*mpLifecycleMutex);
        if (mApp == nullptr || !mApp->isIdle()) {
            return false;
        }
    }

    // Still idle when destroy() runs. A request is the only thing that starts
    // a process, and every request is sent from the Backend thread -- this one.
    // With neither process loaded, nothing on the app's side can ask for one.
    return destroy();
}

bool Application::waitUntilIdle(uint32_t timeoutMs)
{
    // Polled rather than signalled: idleness is two values the app thread
    // publishes (see AppInstance::isIdle()), and the one caller waits tens of
    // milliseconds, on a launch that has already failed once. Counted in
    // polls rather than timed: GetTimeMs() does not wrap at 2^32, so a
    // difference of two readings is wrong across its wrap.
    static constexpr uint32_t skPollMs = 10;

    for (uint32_t waited = 0; ; waited += skPollMs) {
        {
            OS::MutexCS cs(*mpLifecycleMutex);
            if (mApp == nullptr || mApp->isIdle()) {
                return true;
            }
        }
        if (waited >= timeoutMs) {
            return false;
        }
        OS::Delay(skPollMs);
    }
}

void Application::dispatchFrameTick(uint32_t timestamp)
{
    OS::MutexCS cs(*mpLifecycleMutex);

    // An app draws only while it is on screen. The timer is already stopped
    // when none is, so this covers a tick in flight across the change. The raw
    // instance is used because the Application wrappers take this same,
    // non-recursive lock.
    if (mApp == nullptr || !mApp->isGuiResumed()) {
        return;
    }

    mApp->onFrameTick(timestamp);
}

void Application::dispatchGlanceTick(uint32_t timestamp)
{
    OS::MutexCS cs(*mpLifecycleMutex);
    if (mApp != nullptr && mApp->isSrvRun()) {
        mApp->onGlanceTick(timestamp);
    }
}

void Application::dispatchButton(uint32_t timestamp,
                                 SDK::Message::EventButton::Id id,
                                 SDK::Message::EventButton::Event event)
{
    // A clockface backs the home screen instead of running in the foreground,
    // so the kernel keeps the whole navigation and the face is never handed a
    // button -- otherwise the same press would be acted on twice, once by the
    // home screen and once by the face. Tested before the lock: the type comes
    // from the header flags and does not change over the app's lifetime.
    if (isType(App::Type::kClockface)) {
        return;
    }

    OS::MutexCS cs(*mpLifecycleMutex);
    if (mApp != nullptr) {
        mApp->onEventButton(timestamp, id, event);
    }
}

bool Application::sendToService(SDK::MessageBase* msg)
{
    // Hold the lock across the queue push so mApp (and the service queue that
    // lives inside it) cannot be destroyed mid-send by the Backend thread.
    OS::MutexCS cs(*mpLifecycleMutex);
    if (mApp == nullptr) {
        return false;
    }
    return mApp->getComm().sendToService(msg);
}

void Application::onFileAppeared(const char *relativePath)
{
    if (!IsTrackedFile(relativePath)) {
        return;
    }

    char fullPath[FS::FileSystem::skMaxPathLen];
    snprintf(fullPath, sizeof(fullPath), "%s%s", mActivityFilePrefix, relativePath);
    LOG_INFO("%s\n", fullPath);
    mActivityFileRegistry.onFileAppeared(fullPath);

    mHasNewActivity = true;
}

void Application::onFileDisappeared(const char *relativePath)
{
    if (!IsTrackedFile(relativePath)) {
        return;
    }

    char fullPath[FS::FileSystem::skMaxPathLen];
    snprintf(fullPath, sizeof(fullPath), "%s%s", mActivityFilePrefix, relativePath);
    LOG_INFO("%s\n", fullPath);
    mActivityFileRegistry.onFileDisappeared(fullPath);
}

bool Application::ExtractAppPrefix(char *buf, size_t bufSize, const char *appFilePath)
{
    // Find first '/' - skip drive prefix "2:"
    const char *firstSlash = strchr(appFilePath, '/');
    if (firstSlash == nullptr) {
        return false;
    }

    // Find last '/' - separates directory from filename
    const char *lastSlash = strrchr(appFilePath, '/');
    if (lastSlash == firstSlash) {
        // No subdirectory, only root slash
        return false;
    }

    size_t len = static_cast<size_t>(lastSlash - firstSlash);
    if (len >= bufSize) {
        return false;
    }

    memcpy(buf, firstSlash, len);
    buf[len] = '\0';
    return true;
}

bool Application::IsTrackedFile(const char *relativePath)
{
    static constexpr char   kActivityPrefix[]  = "/Activity/";
    static constexpr size_t kActivityPrefixLen = sizeof(kActivityPrefix) - 1;

    if (strncmp(relativePath, kActivityPrefix, kActivityPrefixLen) != 0) {
        return false;
    }

    const char *ext = pathGetExtension(relativePath);
    return ext != nullptr && strEqualCI(ext, ".fit");
}

AppInstance::AppInstance(App::Info &info,
                         MessageManager &messageMgr,
                         LifeCycleCallback *cb,
                         Application *base,
                         AppSurface &surface)
    : mInfo(info)
    , mMessageMgr(messageMgr)
    , mAppComm(mInfo.srvPID, mInfo.guiPID, mMessageMgr)
    , mUserFs(FS::FileSystem::GetInstance())
    , mFsAPI(mUserFs)
    , mSrvKernel(mSrvName, *mAppComm.getServiceComm(), mMessageMgr, mFsAPI)
    , mGuiKernel(mGuiName, *mAppComm.getGuiComm(), mMessageMgr, mFsAPI)
    , mSrvLoader(mSrvName)
    , mGuiLoader(mGuiName)
    , mLifeCycleCb(cb)
    , mBase(base)
    , mSurface(surface)
    , mCapabilities {}
    , mGlanceData {}
    , mMutex{}
    , mFlags {}
    , mThread {}
    , mGuiTickNumber(0)
    , mGlanceTickNumber(0)
    , mStartGuiTime(0)
    , mSrvSensorListener(mAppComm, true)
    , mGuiSensorListener(mAppComm, false)
{
    snprintf(mSrvName, sizeof(mSrvName), "%s.SRV", mInfo.name);
    snprintf(mGuiName, sizeof(mGuiName), "%s.GUI", mInfo.name);

    // Create a path for the application location directory
    char location[strlen(mInfo.file->getPath()) + 1] { };

    // Any absolute address must contain the "/" character.
    char *end = strrchr(mInfo.file->getPath(), '/');
    size_t len = end - mInfo.file->getPath() + 1;
    if (!end) {
        LOG_ERROR("Invalid path '%s'\n", mInfo.file->getPath());
        mUserFs.setRootDirPath(APPS_DIR_PATH"Unknown");
    } else {
        strncpy(location, mInfo.file->getPath(), len);
    }

    mUserFs.setRootDirPath(location);
    mUserFs.enableVolumeRoots(true);  // apps may read ":/Workouts"
    mUserFs.setFileEventListener(base);

    mMutex.init("AppMutex");
    mFlags.init("AppFlags");
}

AppInstance::~AppInstance()
{
    if (isInitialized()) {
        deinit();
    }
    mGuiLoader.unload();

    // The GUI thread is terminated by the unload above, so it
    // can no longer hold or return anything. Reclaim whatever
    // it still owed -- in particular messages parked in the
    // SDK's own user queue, which the drain above cannot see.
    // Here rather than in deinit(): deinit runs before the
    // unloads on some paths, and sweeping a live process frees
    // messages out from under it.
    mMessageMgr.releaseByOwner(mInfo.guiPID);
    mSrvLoader.unload();

    // As for the GUI above: the thread is gone, so anything
    // still owed can be reclaimed safely.
    //
    // Both sweeps here assume no OTHER live instance shares these PIDs. PIDs
    // are derived from the app's path, so a relaunch of the same .uapp gets
    // the same ones, and the owner table cannot tell two incarnations apart --
    // a sweep for the old one would take the new one's claims. That holds only
    // because create() and destroy() are serialised on the Backend thread, and
    // this destructor runs inside destroy(). It is not a property of the table
    // itself, so a future asynchronous teardown would need a per-incarnation
    // token rather than a raw PID.
    mMessageMgr.releaseByOwner(mInfo.srvPID);
}

bool AppInstance::init()
{
    LOG_INFO("[%s] Initialization. SRV: 0x%08X, GUI: 0x%08X\n", mInfo.name, mInfo.srvPID, mInfo.guiPID);

    InitState expected = InitState::None;
    if (!mInitState.compare_exchange_strong(expected, InitState::InProgress)) {
        LOG_WARNING("[%s] Already initializing or initialized. State %u\n", mInfo.name, expected);
        return expected == InitState::Initialized;
    }

    if (!mThread.init("AppThread", this, &AppInstance::thread, osPriorityNormal, 1000)) {
        LOG_ERROR("[%s] thread.init timeout\n", mInfo.name);
        mInitState.store(InitState::None);
        return false;
    }

    mInitState.store(InitState::Initialized);
    LOG_DEBUG("[%s] Done\n", mInfo.name);
    return true;
}

bool AppInstance::deinit()
{
    LOG_INFO("[%s] Deinitialization. SRV: 0x%08X, GUI: 0x%08X\n", mInfo.name, mInfo.srvPID, mInfo.guiPID);

    InitState expected = InitState::Initialized;
    if (!mInitState.compare_exchange_strong(expected, InitState::InProgress)) {
        LOG_WARNING("[%s] Not initialized or already deinitialized. State %u\n",  mInfo.name, expected);
        return expected == InitState::None;
    }

    mThread.requestStop();
    if(!mThread.join(5000)) {
        LOG_ERROR("[%s] thread.join timeout\n", mInfo.name);
    }
    mThread.deinit();

    // Stop the sensor producers before clearing the queues: the refresh
    // thread only stops delivering to a listener once disconnect() runs, so
    // clearing first would leave a window for a late sample to land in the
    // cleared queue and orphan a message-pool block. Disconnecting first also
    // covers the destruction path (a direct deinit must not leave a driver
    // refreshing into a freed listener member). no-ops if already released.
    Sensor::Manager::getInstance().disconnectListener(&mGuiSensorListener);
    Sensor::Manager::getInstance().disconnectListener(&mSrvSensorListener);

    // Make sure that we return all messages to the pool
    mAppComm.clearAllQueues();

    mInitState.store(InitState::None);

    LOG_DEBUG("[%s] Done \n", mInfo.name);
    return true;
}

bool AppInstance::isInitialized() const
{
    return mInitState.load() == InitState::Initialized;
}

bool AppInstance::isIdle() const
{
    // The count first. The app thread lowers it only after publishing what
    // the handler left loaded, so reading it first is what makes the read of
    // mProcessesLoaded after it current.
    return mRequestsInFlight.load() == 0 && !mProcessesLoaded.load();
}

bool AppInstance::run(uint32_t timeout)
{
    // Before the early return, not after it: any path that returns false
    // must leave a reason belonging to THIS attempt. Otherwise a second
    // launch of an instance whose first attempt ran out of memory reports
    // out of memory again, for an unrelated cause.
    mLaunchFailure = LaunchFailure::OTHER;

    if (!isInitialized()) {
        return false;
    }
    LOG_INFO("[%s] Run request\n", mInfo.name);

    // Set here rather than in the handler: the handler runs on the app's own
    // thread and may never reach it (a full queue, or no answer inside the
    // timeout), in which case this is the only reason there is. Anything that
    // does run overwrites it.
    mLaunchFailure = LaunchFailure::TIMEOUT;

    auto* msg = mMessageMgr.allocateMessage<App::InternalAppRun>();
    if (msg == nullptr) {
        // The pool is empty, which is a memory failure and should say so.
        // sendToAppAndRelease returns false for a null message, and the
        // TIMEOUT set above would otherwise stand and report the generic
        // message for the one cause the user can act on.
        mLaunchFailure = LaunchFailure::OUT_OF_MEMORY;
        return false;
    }

    if (!sendToAppAndRelease(msg, timeout)) {
        return false;
    }

    // Success clears the reason set up front, so the value matches what
    // getLaunchFailure() documents and a reader that checks it without
    // checking the return value is not told the launch timed out.
    mLaunchFailure = LaunchFailure::NONE;
    return true;
}

bool AppInstance::runService(uint32_t timeout)
{
    if (!isInitialized()) {
        return false;
    }

    auto* msg = mMessageMgr.allocateMessage<App::InternalAppRunService>();
    return sendToAppAndRelease(msg, timeout);
}

bool AppInstance::stop(uint32_t timeout)
{
    if (!isInitialized()) {
        return true; // App isn't running
    }

    LOG_INFO("[%s] Stop request\n", mInfo.name);

    mFlags.clear(Flag::Stopped);
    auto* msg = mMessageMgr.allocateMessage<App::InternalAppStop>();
    sendToAppAndRelease(msg, 0);

    // Cap the wait: stopAll() passes osWaitForever, but Flag::Stopped is set
    // only once the app acknowledges the stop (via Kernel::exit() ->
    // REQUEST_APP_TERMINATE). A wedged service never acknowledges, so an
    // unbounded wait would freeze the caller (Backend thread) and the whole
    // device. graceMs is the contract deadline measured from the stop request.
    uint32_t graceMs = (timeout < skStopGraceMs) ? timeout : skStopGraceMs;
    if (mFlags.wait(Flag::Stopped, osFlagsWaitAny, graceMs)) {
        return true;
    }

    // A fire-and-forget caller (timeout == 0) does not escalate: the app stops
    // asynchronously and this just reports "not stopped yet".
    if (timeout == 0) {
        return false;
    }

    // Contract breached: no stop-acknowledgement within the grace window, so
    // the app is genuinely wedged (not merely idle). Force-terminate it. This
    // can strand resources the wedged thread held (e.g. the FatFs mutex); that
    // is an accepted last resort, since a permanently frozen device is worse.
    LOG_WARNING("[%s] Stop not acknowledged in %u ms; forcing termination\n",
                mInfo.name, graceMs);
    forceTerminateService();

    return mFlags.wait(Flag::Stopped, osFlagsWaitAny, skStopForceMs);
}

void AppInstance::forceTerminateService()
{
    auto* msg = mMessageMgr.allocateMessage<SDK::Message::RequestAppTerminate>();
    if (msg == nullptr) {
        LOG_ERROR("[%s] No message to force termination\n", mInfo.name);
        return;
    }

    // Tag with the service PID so appLifeCycle() runs the full srv+gui teardown
    // branch (which unloads both processes and sets Flag::Stopped), not the
    // GUI-only path. Delivered on the common queue -> handled by the dispatch
    // thread, which is alive even when the service process thread is wedged.
    msg->setProcessId(mInfo.srvPID);
    mAppComm.sendToApp(msg, 0);
    mMessageMgr.releaseMessage(msg);
}

bool AppInstance::suspend(uint32_t timeout)
{
    if (!isInitialized()) {
        return false;
    }

    auto* msg = mMessageMgr.allocateMessage<App::InternalAppSuspend>();
    return sendToAppAndRelease(msg, timeout);
}

bool AppInstance::resume(uint32_t timeout)
{
    if (!isInitialized()) {
        return false;
    }

    auto* msg = mMessageMgr.allocateMessage<App::InternalAppResume>();
    return sendToAppAndRelease(msg, timeout);
}

bool AppInstance::runGlance(uint32_t timeout)
{
    if (!isInitialized()) {
        return false;
    }

    auto* msg = mMessageMgr.allocateMessage<App::InternalAppGlanceRun>();
    return sendToAppAndRelease(msg, timeout);
}

bool AppInstance::runGuiForService(uint32_t timeout)
{
    if (!isInitialized()) {
        return false;
    }

    auto* msg = mMessageMgr.allocateMessage<App::InternalAppRunGui>();
    return sendToAppAndRelease(msg, timeout);
}

bool AppInstance::stopGlance(uint32_t timeout)
{
    if (!isInitialized()) {
        return false;
    }

    auto* msg = mMessageMgr.allocateMessage<App::InternalAppGlanceStop>();
    return sendToAppAndRelease(msg, timeout);
}

App::Capabilities AppInstance::getCapabilities()
{
    OS::MutexCS cs(mMutex);
    return mCapabilities;
}

App::GlanceData AppInstance::getGlanceData()
{
    OS::MutexCS cs(mMutex);
    return mGlanceData;
}

void AppInstance::onFrameTick(uint32_t timestamp)
{
    if (isGuiRun()) {
        auto* msg = mMessageMgr.allocateMessage<SDK::Message::EventGuiTick>();
        if (msg) {
            msg->timestamp = timestamp;
            msg->frameNumber = mGuiTickNumber;
            mAppComm.sendToGui(msg);
            mMessageMgr.releaseMessage(msg);
        }
        mGuiTickNumber++;

        // Check GUI activity
        if (!mFlags.isSet(Flag::GuiIsActive) &&
                OS::GetTimeMs() - mStartGuiTime > 3000) {
            LOG_ERROR("[%s] Does not respond\n", mInfo.name);

            // Ask the app's kernel to terminate it: the request goes to the
            // kernel queue, not to the unresponsive thread. Terminating a
            // thread at an arbitrary point can leave resources it holds.

            auto* msg = mMessageMgr.allocateMessage<SDK::Message::RequestAppTerminate>();
            if (msg) {
                mAppComm.sendToApp(msg, 0);
                mMessageMgr.releaseMessage(msg);
            }
        }
    }
}

void AppInstance::onGlanceTick(uint32_t timestamp)
{
    if (isSrvRun()) {
        auto* msg = mMessageMgr.allocateMessage<SDK::Message::EventGlanceTick>();
        if (msg) {
            msg->timestamp = timestamp;
            msg->frameNumber = mGlanceTickNumber;
            mAppComm.sendToService(msg);
            mMessageMgr.releaseMessage(msg);
        }
        mGlanceTickNumber++;
    }
}

void AppInstance::onGlanceStop()
{

}

void AppInstance::onEventButton(uint32_t timestamp, SDK::Message::EventButton::Id id, SDK::Message::EventButton::Event event)
{
    if (isGuiResumed()) {
        auto* msg = mMessageMgr.allocateMessage<SDK::Message::EventButton>();
        if (msg) {
            msg->timestamp = timestamp;
            msg->id = id;
            msg->event = event;
            mAppComm.sendToGui(msg);
            mMessageMgr.releaseMessage(msg);
        }
    }
}



void AppInstance::thread()
{
    LOG_INFO("[%s] Initialized\n", mInfo.name);

    // Initialization successful
    mThread.setInitialized();

    // Register Queue for automatic interruption
    OS::CancellableGuard guard(&mThread, mAppComm.getAppCancellable());

    // Main loop
    while (!mThread.shouldStop()) {

        // Wait for message (blocks until available)
        SDK::MessageBase* msg = nullptr;
        mAppComm.receiveFromApp(msg, osWaitForever);

        assert(0 == __get_BASEPRI());

        if (mThread.shouldStop()) {
            LOG_DEBUG("[%s] Interrupted\n", mInfo.name);
            if (msg) {
                // Release message
                mMessageMgr.releaseMessage(msg);
            }
            break;
        }

        if (msg == nullptr) {
            LOG_ERROR("[%s] msg == nullptr\n", mInfo.name);
            continue;
        }

        LOG_DEBUG("[%s] Message 0x%08X received \n", mInfo.name, msg->getType());

        // Dispatch
        appLifeCycle(msg);
        appMsgHandler(msg);
        slMsgHandler(msg);

        // Default handler
        if (msg->getResult() == SDK::MessageResult::PENDING) {
            msg->setResult(SDK::MessageResult::FAIL);
            mMessageMgr.signalCompletion(msg);
        }

        // Read before the release, not after: that release can be the last
        // one, in which case the block is back in the pool and may already
        // have been handed to somebody else by the time this line runs.
        const uint32_t releasedType = static_cast<uint32_t>(msg->getType());

        // After the handlers, and in this order: a reader that sees the count
        // fall must also see what they left loaded (see isIdle()). Published
        // after every message, counted or not, because the unload that ends
        // an app happens in the terminate its own process sends.
        mProcessesLoaded.store(mSrvLoader.isLoaded() || mGuiLoader.isLoaded());
        if (IsLifecycleRequest(releasedType)) {
            mRequestsInFlight.fetch_sub(1);
        }

        // Release message
        mMessageMgr.releaseMessage(msg);
        LOG_DEBUG("[%s] Message 0x%08X released \n", mInfo.name, releasedType);

        // Asked after every message rather than after the ones that stop
        // something: a launch that fails stops nothing, and leaves the
        // instance just as idle as an exit does.
        if (isIdle() && mLifeCycleCb && mBase) {
            mLifeCycleCb->onIdle(mBase);
        }
    }

    // Terminate threads
    if (isGuiRun()) {
        stopGui();
        if (mLifeCycleCb && mBase) {
            mLifeCycleCb->onGuiStop(mBase);
        }
    }

    if (isSrvRun()) {
        stopSrv();
        if (mLifeCycleCb && mBase) {
            mLifeCycleCb->onSrvStop(mBase);
        }
    }

    mAppComm.clearAllQueues();

    LOG_INFO("[%s] Deinitialized\n", mInfo.name);
}

bool AppInstance::sendToAppAndRelease(SDK::MessageBase* msg, uint32_t timeout)
{
    if (!msg) {
        return false;
    }

    bool result = false;

    sendToAppQueue(msg, timeout);
    result = msg->getResult() == SDK::MessageResult::SUCCESS;

    mMessageMgr.releaseMessage(msg);

    return result;
}

bool AppInstance::sendToAppQueue(SDK::MessageBase* msg, uint32_t timeout)
{
    // Raised before the push: once it is in the queue the app thread may
    // handle it and lower the count before this thread gets control back.
    const bool counted = IsLifecycleRequest(msg->getType());
    if (counted) {
        mRequestsInFlight.fetch_add(1);
    }

    if (!mAppComm.sendToApp(msg, timeout)) {
        if (counted) {
            mRequestsInFlight.fetch_sub(1);
        }
        LOG_ERROR("[%s] Failed to send msg 0x%08X to the app queue. timeout %u ms\n",
                mInfo.name, msg->getType(), timeout);
        return false;
    }

    return true;
}

// Both loaders read from codeFile -- the target .uapp for variants, the
// app's own file otherwise. The blob offsets must come from the code file's
// header (an alias's header has serviceSize = 0).
bool AppInstance::loadService()
{
    MainHeader mainHeader;
    if (!Loader::ReadMainHeader(*mInfo.codeFile.get(), mainHeader)) {
        LOG_ERROR("Failed to load a main header\n");
        // Set here, not taken from the loader: the loader never ran, so its
        // reason belongs to some earlier attempt.
        mLaunchFailure = LaunchFailure::BAD_IMAGE;
        return false;
    }

    uint32_t offset = sizeof(MainHeader) +
                      mainHeader.normal_icon_size +
                      mainHeader.small_icon_size;

    if (!mSrvLoader.load(*mInfo.codeFile.get(), *mSrvKernel.getInterface(), offset)) {
        mLaunchFailure = mSrvLoader.getFailure();
        return false;
    }

    return true;
}

bool AppInstance::loadGui()
{
    MainHeader mainHeader;
    if (!Loader::ReadMainHeader(*mInfo.codeFile.get(), mainHeader)) {
        LOG_ERROR("Failed to load a main header\n");
        // As in loadService(): the loader never ran, so do not ask it.
        mLaunchFailure = LaunchFailure::BAD_IMAGE;
        return false;
    }


    uint32_t offset = sizeof(MainHeader) +
                          mainHeader.normal_icon_size +
                          mainHeader.small_icon_size +
                          mainHeader.serviceSize;

    if (!mGuiLoader.load(*mInfo.codeFile.get(), *mGuiKernel.getInterface(), offset)) {
        mLaunchFailure = mGuiLoader.getFailure();
        return false;
    }

    return true;
}

bool AppInstance::runSrv()
{
    if (isSrvRun()) {
        LOG_DEBUG("[%s] SRV already running\n", mInfo.name);
        return true;
    }

    bool result = false;
    LOG_DEBUG("[%s] Running...\n", mInfo.name);

    result = loadService();

    if (result) {
        mSrvState = AppPartState::RUN;
    } else {
        mSrvState = AppPartState::NONE;
        mSrvLoader.unload();

        // As for the GUI above: the thread is gone, so anything
        // still owed can be reclaimed safely.
        mMessageMgr.releaseByOwner(mInfo.srvPID);
        mSrvKernel.reset();

        LOG_ERROR("Failed to load service\n");
    }

    LOG_DEBUG("[%s] %s\n", mInfo.name, result ? "Done" : "Failed");
    return result;
}

bool AppInstance::stopSrv()
{
    if (!isSrvRun()) {
        LOG_DEBUG("[%s] SRV already stopped\n", mInfo.name);
        return true;
    }

    bool result = false;
    LOG_DEBUG("[%s] Stop...\n", mInfo.name);

    // Force stop
    mSrvState = AppPartState::NONE;

    // Skip pending messages, they no longer make sense
    mAppComm.clearServiceQueue();

    auto* msg = mMessageMgr.allocateMessage<SDK::Message::CommandAppStop>();
    if (msg) {
        // Give the process time to complete correctly.
        // This is only notification
        result = mAppComm.sendToService(msg);
        mMessageMgr.releaseMessage(msg);
    }

    LOG_DEBUG("[%s] %s\n", mInfo.name, result ? "Done" : "Failed");
    return result;
}

bool AppInstance::runGui()
{
    if (isGuiRun()) {
        LOG_DEBUG("[%s] GUI already running\n", mInfo.name);
        return true;
    }

    bool result = false;
    LOG_DEBUG("[%s] Running...\n", mInfo.name);

    result = loadGui();

    if (result) {
        mGuiState = AppPartState::RUN;
        mStartGuiTime = OS::GetTimeMs();

        // Notify the service
        auto *notif = mMessageMgr.allocateMessage<SDK::Message::CommandAppNotifGuiRun>();
        if (notif) {
            mAppComm.sendToService(notif);
            mMessageMgr.releaseMessage(notif);
        }

        // Send first tick event to update screen ASAP
        onFrameTick(OS::GetTimeMs());

    } else {
        mGuiLoader.unload();

        // The GUI thread is terminated by the unload above, so it
        // can no longer hold or return anything. Reclaim whatever
        // it still owed -- in particular messages parked in the
        // SDK's own user queue, which the drain above cannot see.
        // Here rather than in deinit(): deinit runs before the
        // unloads on some paths, and sweeping a live process frees
        // messages out from under it.
        mMessageMgr.releaseByOwner(mInfo.guiPID);
        mGuiKernel.reset();
        mGuiState = AppPartState::NONE;
        mStartGuiTime = 0;

        LOG_ERROR("Failed to load GUI\n");
    }

    LOG_DEBUG("[%s] %s\n", mInfo.name, result ? "Done" : "Failed");
    return result;
}

bool AppInstance::stopGui()
{
    if (!isGuiRun()) {
        LOG_DEBUG("[%s] GUI already stopped\n", mInfo.name);
        return true;
    }

    bool result = false;
    LOG_DEBUG("[%s] Stop...\n", mInfo.name);

    // Force stop
    mGuiState = AppPartState::NONE;
    mStartGuiTime = 0;

    // Skip pending messages, they no longer make sense
    mAppComm.clearGuiQueue();

    auto* msg = mMessageMgr.allocateMessage<SDK::Message::CommandAppStop>();
    if (msg) {
        // Give the process time to complete correctly.
        // This is only notification
        result = mAppComm.sendToGui(msg);
        mMessageMgr.releaseMessage(msg);

        if (result) {
            // Notify the service that the GUI will be stopped
            auto *notif = mMessageMgr.allocateMessage<SDK::Message::CommandAppNotifGuiStop>();
            if (notif) {
                mAppComm.sendToService(notif);
                mMessageMgr.releaseMessage(notif);
            }
        }
    }

    LOG_DEBUG("[%s] %s\n", mInfo.name, result ? "Done" : "Failed");
    return result;
}

bool AppInstance::resumeGui()
{
    if (!isGuiRun()) {
        LOG_DEBUG("[%s] GUI not running\n", mInfo.name);
        return false;
    }

    if (isGuiResumed()) {
        LOG_DEBUG("[%s] GUI already resumed\n", mInfo.name);
        return true;
    }

    bool result = false;
    LOG_DEBUG("[%s] Resuming...\n", mInfo.name);

    auto* msg = mMessageMgr.allocateMessage<SDK::Message::CommandAppGuiResume>();
    if (msg) {
        result = mAppComm.sendToGui(msg);
        mGuiState = AppPartState::RESUMED;
        mMessageMgr.releaseMessage(msg);

        // Restart the "has it drawn yet" window. A GUI only writes a frame
        // while it is resumed, so this is the point from which one can be
        // expected -- measuring from the load instead would condemn any app
        // whose first resume comes more than the grace period after it, which
        // is the normal case for a watch face loaded at boot.
        mStartGuiTime = OS::GetTimeMs();

        // Draw at once instead of waiting for the next tick: for a slowly
        // paced GUI that wait is a visible delay before the screen is its own.
        onFrameTick(mStartGuiTime);
    }

    LOG_DEBUG("[%s] %s\n", mInfo.name, result ? "Done" : "Failed");
    return result;
}

bool AppInstance::suspendGui()
{
    if (!isGuiResumed()) {
        LOG_DEBUG("[%s] GUI already suspended or not running\n", mInfo.name);
        return true;
    }

    bool result = false;
    LOG_DEBUG("[%s] Suspend...\n", mInfo.name);

    auto* msg = mMessageMgr.allocateMessage<SDK::Message::CommandAppGuiSuspend>();
    if (msg) {
        result = mAppComm.sendToGui(msg);
        mMessageMgr.releaseMessage(msg);
    }

    // Force suspend
    mGuiState = AppPartState::RUN;

    LOG_DEBUG("[%s] %s\n", mInfo.name, result ? "Done" : "Failed");
    return result;
}

bool AppInstance::runGlc()
{
    if (!isSrvRun()) {
        LOG_DEBUG("[%s] Service not running\n", mInfo.name);
        return false;
    }

    if (isGlanceRun()) {
        LOG_DEBUG("[%s] Glance already activated\n", mInfo.name);
        return true;
    }

    bool result = false;
    LOG_DEBUG("[%s] Glance activating...\n", mInfo.name);

    auto* msg = mMessageMgr.allocateMessage<SDK::Message::EventGlanceStart>();
    if (msg) {
        result = mAppComm.sendToService(msg);
        mSrvState = AppPartState::GLANCE;
        mMessageMgr.releaseMessage(msg);
    }

    // Send first tick event to update screen ASAP
    onGlanceTick(OS::GetTimeMs());

    LOG_DEBUG("[%s] %s\n", mInfo.name, result ? "Done" : "Failed");
    return result;
}

bool AppInstance::stopGlc()
{
    if (!isGlanceRun()) {
        LOG_DEBUG("[%s] Glance already deactivated or service not running\n", mInfo.name);
        return true;
    }

    bool result = false;
    LOG_DEBUG("[%s] Glance deactivation...\n", mInfo.name);

    auto* msg = mMessageMgr.allocateMessage<SDK::Message::EventGlanceStop>();
    if (msg) {
        result = mAppComm.sendToService(msg);
        mMessageMgr.releaseMessage(msg);
    }

    // Force deactivate glance
    mSrvState = AppPartState::RUN;

    LOG_DEBUG("[%s] %s\n", mInfo.name, result ? "Done" : "Failed");
    return result;
}

void AppInstance::appLifeCycle(SDK::MessageBase* msg)
{
    if (!msg) {
        return;
    }

    switch (msg->getType()) {
        case App::InternalMessageType::APP_RUN_SRV: {
            bool status = runSrv();
            if (msg->getResult() == SDK::MessageResult::TIMEOUT) {
                // Should be stopped as we are not feet in time
                stopSrv();
            } else {
                msg->setResult(status ? SDK::MessageResult::SUCCESS : SDK::MessageResult::FAIL);
                mMessageMgr.signalCompletion(msg);

                if (status && mLifeCycleCb && mBase) {
                    mLifeCycleCb->onSrvStart(mBase);
                }
            }
        } break;

        case App::InternalMessageType::APP_RUN_SRV_ANG_GUI: {
            bool status = runSrv();
            if (status) {
                status = runGui();
                if (!status) {
                    stopSrv();
                }
            }

            if (msg->getResult() == SDK::MessageResult::TIMEOUT) {
                // Should be stopped as we are not feet in time
                stopGui();
                stopSrv();
            } else {
                msg->setResult(status ? SDK::MessageResult::SUCCESS : SDK::MessageResult::FAIL);
                mMessageMgr.signalCompletion(msg);

                if (status && mLifeCycleCb && mBase) {
                    mLifeCycleCb->onSrvStart(mBase);
                    mLifeCycleCb->onGuiStart(mBase);
                }
            }
        } break;

        case App::InternalMessageType::APP_STOP_SRV_ANG_GUI: {
            bool isGuiWasRun = isGuiRun();
            bool isSrvWasRun = isSrvRun();

            if (isGuiWasRun) {
                stopGui();
            }
            if (isSrvWasRun) {
                stopSrv();
            }

            if (!isSrvWasRun) {
                // If the service has not been started,
                // then there is no point in waiting for
                // REQUEST_APP_TERMINATE because it will
                // not be there, we set the flag here
                mFlags.set(Flag::Stopped);
            } else {
                // Waiting Flag::Stopped in REQUEST_APP_TERMINATE
            }

            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);

            // Run timer for stop confirmation
            //TODO:
        } break;

        case App::InternalMessageType::APP_SUSPEND_GUI: {
            if (isGuiRun()) {
                bool wasResumed = isGuiResumed();
                suspendGui();
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);

                if (wasResumed && mLifeCycleCb && mBase) {
                    mLifeCycleCb->onGuiSuspend(mBase);
                }
            } else {
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::FAIL);
            }
        } break;

        case App::InternalMessageType::APP_RESUME_GUI: {
            if (isGuiRun()) {
                bool wasResumed = isGuiResumed();
                bool status = resumeGui();

                if (!wasResumed && status && msg->getResult() == SDK::MessageResult::TIMEOUT) {
                    suspendGui();
                    // no response needed as timeout occurs
                } else {
                    msg->setResult(status ? SDK::MessageResult::SUCCESS : SDK::MessageResult::FAIL);
                    mMessageMgr.signalCompletion(msg);

                    if (status && mLifeCycleCb && mBase) {
                        mLifeCycleCb->onGuiResume(mBase);
                    }
                }
            } else {
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::FAIL);
            }
        } break;

        case App::InternalMessageType::APP_GLANCE_RUN: {
            bool isSrvWasRun = isSrvRun();
            bool status = runSrv();
            if (status) {
                status = runGlc();
                if (!isSrvWasRun && !status) {
                    // cleanup state
                    stopSrv();
                }
            }

            msg->setResult(status ? SDK::MessageResult::SUCCESS : SDK::MessageResult::FAIL);
            mMessageMgr.signalCompletion(msg);

            if (status && mLifeCycleCb && mBase) {
                if (!isSrvWasRun) {
                    mLifeCycleCb->onSrvStart(mBase);
                }
                mLifeCycleCb->onGlanceRun(mBase);
            }
        } break;

        case App::InternalMessageType::APP_GLANCE_STOP: {
            if (isSrvRun()) {
                bool isGlanceWasRun = isGlanceRun();
                stopGlc();
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);

                if (isGlanceWasRun && mLifeCycleCb && mBase) {
                    mLifeCycleCb->onGlanceStop(mBase);
                }
            } else {
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::FAIL);
            }
        } break;

        case SDK::MessageType::REQUEST_APP_TERMINATE: {
            bool isGuiStopped = false;
            bool isSrvStopped = false;

            auto* terminateMsg = static_cast<SDK::Message::RequestAppTerminate*>(msg);
            if (terminateMsg->getProcessId() == mInfo.srvPID) {
                // Stop the sensor producers BEFORE the queues are cleared
                // below. The Sensor::Manager refresh thread only stops
                // delivering to a listener once disconnect() (via unRegSensor)
                // runs -- not when the app queues are cleared or the app thread
                // is joined. Disconnecting first lets the clearGuiQueue() /
                // clearServiceQueue() calls drain the final samples, so nothing
                // new can land in an already-cleared queue and orphan a
                // message-pool block. Also releases duty-cycled sensors (e.g.
                // GPS) that a graceful stop would. no-ops for drivers never
                // connected to.
                Sensor::Manager::getInstance().disconnectListener(&mGuiSensorListener);
                Sensor::Manager::getInstance().disconnectListener(&mSrvSensorListener);

                // Terminate GUI first
                if (mGuiLoader.isLoaded()) {

                    // Give the GUI up to about 100 ms to become ready to
                    // delete. The unload goes ahead either way, so a GUI
                    // thread that isn't ready can leave resources held.

                    mGuiKernel.waitReadyToDelete();
                    mAppComm.clearGuiQueue();
                    mGuiLoader.unload();

                    // The GUI thread is terminated by the unload above, so
                    // it can no longer hold or return anything, and its claims
                    // can no longer change -- which is what makes the sweep
                    // safe without locking. Reclaim whatever it still owed, in
                    // particular messages parked in the SDK's own user queue,
                    // which no kernel drain can see.
                    //
                    // AFTER the unload, never before: a claim is only ever
                    // added or removed by the process that owns it, so the
                    // sweep is race-free exactly once that thread is gone.
                    mMessageMgr.releaseByOwner(mInfo.guiPID);
                    mGuiKernel.reset();

                    mGuiState = AppPartState::NONE;
                    mGuiTickNumber = 0;
                    mStartGuiTime = 0;
                    mFlags.clear(Flag::GuiIsActive);
                    LOG_INFO("[%s] Process with PID 0x%08X terminated\n", mInfo.name, mInfo.guiPID);
                    isGuiStopped = true;
                }

                if (mSrvLoader.isLoaded()) {

                    mSrvKernel.waitReadyToDelete();
                    mAppComm.clearGuiQueue(); // Double check. The service could send something while the GUI was stopped.
                    mAppComm.clearServiceQueue();
                    mSrvLoader.unload();

                    // As for the GUI above: the thread is gone, so anything
                    // still owed can be reclaimed safely.
                    mMessageMgr.releaseByOwner(mInfo.srvPID);
                    mSrvKernel.reset();

                    mSrvState = AppPartState::NONE;
                    mGlanceTickNumber = 0;
                    LOG_INFO("[%s] Process with PID 0x%08X terminated\n", mInfo.name, mInfo.srvPID);
                    isSrvStopped = true;

                    // Safety net: a crashing/exiting app must never leave a
                    // strap link or scan running.
                    App::AccessoryManager::getInstance().onAppStopped(mInfo.srvPID);
                }
            } else {
                if (mGuiLoader.isLoaded()) {
                    bool isGuiWasRun = isGuiRun();

                    // Stop the GUI sensor producer before clearing its queue
                    // (see the whole-app branch note above): disconnect() halts
                    // the refresh thread's delivery, so a late sample cannot
                    // land in the just-cleared queue and orphan a pool block.
                    // no-ops for drivers never connected to.
                    Sensor::Manager::getInstance().disconnectListener(&mGuiSensorListener);

                    mGuiKernel.waitReadyToDelete();
                    mAppComm.clearGuiQueue();
                    mGuiLoader.unload();

                    // The GUI thread is terminated by the unload above, so
                    // it can no longer hold or return anything, and its claims
                    // can no longer change -- which is what makes the sweep
                    // safe without locking. Reclaim whatever it still owed, in
                    // particular messages parked in the SDK's own user queue,
                    // which no kernel drain can see.
                    //
                    // AFTER the unload, never before: a claim is only ever
                    // added or removed by the process that owns it, so the
                    // sweep is race-free exactly once that thread is gone.
                    mMessageMgr.releaseByOwner(mInfo.guiPID);
                    mGuiKernel.reset();

                    mGuiState = AppPartState::NONE;
                    mGuiTickNumber = 0;
                    mStartGuiTime = 0;
                    mFlags.clear(Flag::GuiIsActive);
                    LOG_INFO("[%s] Process with PID 0x%08X terminated\n", mInfo.name, mInfo.guiPID);
                    isGuiStopped = true;

                    if (isGuiWasRun) {
                        // Notify the service

                        // Reliable sending
                        SDK::Message::CommandAppNotifGuiStop* notif = nullptr;

                        // 1. Allocate retry
                        for (uint32_t cnt = 0; cnt < 3 && notif == nullptr; ++cnt) {
                            notif = mMessageMgr.allocateMessage<SDK::Message::CommandAppNotifGuiStop>();
                            if (notif == nullptr) {
                                LOG_WARNING("Failed to allocate message. Retry\n");
                                OS::Delay(100);
                            }
                        }
                        if (notif == nullptr) {
                            LOG_ERROR("Failed to allocate message after 3 retries\n");
                        } else {
                            // 2. Send retry
                            bool sent = false;
                            for (uint32_t cnt = 0; cnt < 3; ++cnt) {
                                if (mAppComm.sendToService(notif)) {
                                    sent = true;
                                    break;
                                } else {
                                    LOG_WARNING("Failed to send message. Retry\n");
                                    OS::Delay(100);
                                }
                            }

                            // 3. Release always
                            mMessageMgr.releaseMessage(notif);
                            if (!sent) {
                                LOG_ERROR("Failed to send message after 3 retries\n");
                            }
                        }
                    }
                }
            }
            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);

            if (mLifeCycleCb && mBase) {
                if (isGuiStopped) {
                    mLifeCycleCb->onGuiStop(mBase);
                }
                if (isSrvStopped) {
                    mLifeCycleCb->onSrvStop(mBase);
                }
            }

            // Finally send stop event
            if (isSrvStopped) {
                mFlags.set(Flag::Stopped);
            }
        } break;

        case SDK::MessageType::REQUEST_APP_RUN_GUI: {
            LOG_DEBUG("[%s] Service requests GUI launch\n", mInfo.name);
            handleRunGui(msg, true);
        } break;

        case App::InternalMessageType::APP_RUN_GUI: {
            LOG_DEBUG("[%s] Kernel starts the GUI for the service\n", mInfo.name);
            handleRunGui(msg, false);
        } break;

        case SDK::MessageType::REQUEST_SET_CAPABILITIES: {
            LOG_DEBUG("[%s] Sets capabilities\n", mInfo.name);
            auto* caps = static_cast<SDK::Message::RequestSetCapabilities*>(msg);

            // Safety saving
            {
                OS::MutexCS cs(mMutex);
                mCapabilities.enPhoneNotification = caps->enPhoneNotification;
                mCapabilities.enUsbChargingScreen = caps->enUsbChargingScreen;
                mCapabilities.enMusicControl = caps->enMusicControl;
            }

            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);

            if (mLifeCycleCb && mBase) {
                mLifeCycleCb->onCapabilities(mBase, mCapabilities);
            }
            // External-accessory acquisition is opt-in via REQUEST_ACCESSORY_
            // PREPARE / _RELEASE (no longer a capability field), so RequestSet-
            // Capabilities stays ABI-stable for apps built against the old layout.
        } break;

        case SDK::MessageType::REQUEST_APP_NEW_ACTIVITY: {
            LOG_DEBUG("[%s] Sets new activity\n", mInfo.name);
            if (mLifeCycleCb && mBase) {
                mLifeCycleCb->onNewActivity(mBase);
            }
        } break;

        default:
            break;
    }
}

void AppInstance::handleRunGui(SDK::MessageBase* msg, bool requestedByService)
{
    // A GUI with no service to run it is not one to start: the service may
    // have exited between a refusal and the kernel's retry.
    bool isGuiWasRun = isGuiRun();
    bool status = isSrvRun() && runGui();
    msg->setResult(status ? SDK::MessageResult::SUCCESS : SDK::MessageResult::FAIL);
    mMessageMgr.signalCompletion(msg);

    if (!isGuiWasRun && status && mLifeCycleCb && mBase) {
        mLifeCycleCb->onGuiStart(mBase);

        // Since the service launched the GUI on its own,
        // we need the kernel to allow context switching to this application.
        mLifeCycleCb->onGuiResumeRequest(mBase);
    }

    if (!status && requestedByService
            && mLaunchFailure.load() == LaunchFailure::OUT_OF_MEMORY
            && mLifeCycleCb && mBase) {
        mLifeCycleCb->onGuiStartFailed(mBase);
    }
}

void AppInstance::appMsgHandler(SDK::MessageBase* msg)
{
    if (!msg) {
        return;
    }

    switch (msg->getType()) {
        case SDK::MessageType::REQUEST_SYSTEM_SETTINGS: {
            LOG_DEBUG("[%s] Requests system settings\n", mInfo.name);
            auto* settings = static_cast<SDK::Message::RequestSystemSettings*>(msg);
            WatchSettings ws = Settings::Manager::GetInstance().getSettings();
            WatchSettingsLocal wsl = Settings::LocalManager::GetInstance().getSettings();

            settings->imperialUnits = ws.unitsImperial;
            settings->timeFormat = (wsl.clock.format == WatchSettingsLocal::Clock::FMT_12H);
            settings->dateMonthFirst =
                    (wsl.clock.dateOrder == WatchSettingsLocal::Clock::DATE_MDY);

            // Both bounds, and both exclusive. The old loop broke *after*
            // writing the element at kMaxThreshold, so it read thresholds[6]
            // from a six-element array and reported a count of seven. Every
            // app clamped that back to its own six, which is why an
            // out-of-bounds read went unnoticed.
            // Not std::min: skMaxHearRateTh is static const, so binding it to a
            // const& odr-uses it and there is no definition to link against --
            // the same reason the buzzer's notesCount clamp below spells it out.
            uint32_t thresholds = HeartRateZones::kMaxThreshold;
            if (thresholds > SDK::Message::RequestSystemSettings::skMaxHearRateTh) {
                thresholds = SDK::Message::RequestSystemSettings::skMaxHearRateTh;
            }

            for (uint32_t i = 0; i < thresholds; i++) {
                settings->heartRateTh[i] = ws.heartRateZones.thresholds[i];
            }
            settings->heartRateCount = static_cast<uint8_t>(thresholds);

            settings->activityMin = ws.dailyGoals.activityMinutes;
            settings->steps = ws.dailyGoals.steps;
            settings->floors = ws.dailyGoals.floors;

            settings->heightCm = ws.heightCm;
            settings->weightKg = ws.weightKg;

            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        case SDK::MessageType::REQUEST_DISPLAY_CONFIG: {
            LOG_DEBUG("[%s] Requests display config\n", mInfo.name);
            auto* displayCfgMsg = static_cast<SDK::Message::RequestDisplayConfig*>(msg);
            mSurface.getResolution(displayCfgMsg->width, displayCfgMsg->height);
            displayCfgMsg->colorDepth = mSurface.getDisplayColorDepth();
            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        case SDK::MessageType::REQUEST_DISPLAY_UPDATE: {
            LOG_DEBUG("[%s] Requests display update\n", mInfo.name);
            auto* displayUpdMsg = static_cast<SDK::Message::RequestDisplayUpdate*>(msg);

            // Whether this frame reaches the screen is the surface's decision,
            // not this app's own state: it is taken only while this app holds
            // the surface. Both edges are racy from here -- an app suspended a
            // moment ago may still be mid-frame, and a resuming one may draw
            // before the handover has run -- so the check belongs where it can
            // be made against the copy rather than before it.
            mSurface.write(mInfo.guiPID, displayUpdMsg->pBuffer);
            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);

            // Confirm GUI activity
            mFlags.set(Flag::GuiIsActive);
        } break;

        case SDK::MessageType::REQUEST_BACKLIGHT_SET: {
            LOG_DEBUG("[%s] Requests backlight\n", mInfo.name);
            auto* blMsg = static_cast<SDK::Message::RequestBacklightSet*>(msg);
            Interface::IBacklight& bl = ComponentFactory::GetInstance().getBacklight();
            if (blMsg->brightness > 0) {
                bl.on(blMsg->autoOffTimeoutMs);
            } else {
                bl.off();
            }
            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        // Not gated on alertMutedFlag: muting covers alerts the watch raises at
        // the user (phone notifications, calls, low battery), not feedback an
        // app produces as part of a session the user deliberately started.
        // Interval phase cues in Running/Treadmill are the reason -- they are
        // the point of the workout and have to sound even when muted.
        case SDK::MessageType::REQUEST_BUZZER_PLAY: {
            LOG_DEBUG("[%s] Requests buzzer\n", mInfo.name);

            auto* buzzMsg = static_cast<SDK::Message::RequestBuzzerPlay*>(msg);
            Buzzer& buzz = Buzzer::GetInstance();

            // notesCount arrives from the app unchecked, and the message only
            // carries skMaxNotes entries. Everything below is sized from it, so
            // bounding it here is enough. A zero count is passed through: an
            // empty melody is how apps stop, e.g. Alarm's stopRinging().
            // Not std::min: skMaxNotes is static const, so binding it to a
            // const& odr-uses it and there is no definition to link against.
            uint32_t count = buzzMsg->notesCount;
            if (count > SDK::Message::RequestBuzzerPlay::skMaxNotes) {
                LOG_WARNING("[%s] buzzer notesCount %lu > %lu, clamped\n", mInfo.name,
                            static_cast<unsigned long>(count),
                            static_cast<unsigned long>(SDK::Message::RequestBuzzerPlay::skMaxNotes));
                count = SDK::Message::RequestBuzzerPlay::skMaxNotes;
            }

            Interface::IBuzzer::Note melody[count];
            for (uint8_t i = 0; i < count; i++) {
                melody[i].level     = (buzzMsg->notes[i].volume / 33);
                melody[i].time      = buzzMsg->notes[i].time;
                // An app built before notes had a frequency sends 0, the
                // default: the kernel zeroes every message it allocates.
                melody[i].frequency = buzzMsg->notes[i].frequency;
            }
            buzz.play(melody, count);

            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        case SDK::MessageType::REQUEST_VIBRO_PLAY: {
            LOG_DEBUG("[%s] Requests vibro\n", mInfo.name);

            auto* vibroMsg = static_cast<SDK::Message::RequestVibroPlay*>(msg);
            Vibro& vibro = Vibro::GetInstance();

            // Bounded like the buzzer above.
            uint32_t count = vibroMsg->notesCount;
            if (count > SDK::Message::RequestVibroPlay::skMaxNotes) {
                LOG_WARNING("[%s] vibro notesCount %lu > %lu, clamped\n", mInfo.name,
                            static_cast<unsigned long>(count),
                            static_cast<unsigned long>(SDK::Message::RequestVibroPlay::skMaxNotes));
                count = SDK::Message::RequestVibroPlay::skMaxNotes;
            }

            Interface::IVibro::Note melody[count];
            for (uint8_t i = 0; i < count; i++) {
                melody[i].effect = vibroMsg->notes[i].effect;
                melody[i].loop   = 0;
                melody[i].pause  = vibroMsg->notes[i].pause;
            }
            vibro.play(melody, count);

            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        case SDK::MessageType::REQUEST_GLANCE_CONFIG: {
            LOG_DEBUG("[%s] Requests glance config\n", mInfo.name);
            auto* gl = static_cast<SDK::Message::RequestGlanceConfig*>(msg);

            gl->width = 240;
            gl->height = 60;
            gl->maxControls = App::kGlanceControlsMax;

            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        case SDK::MessageType::REQUEST_GLANCE_UPDATE: {
            LOG_DEBUG("[%s] Requests glance update\n", mInfo.name);
            auto* gl = static_cast<SDK::Message::RequestGlanceUpdate*>(msg);

            // Safety saving
            {
                OS::MutexCS cs(mMutex);
                mGlanceData.count = 0;

                snprintf(mGlanceData.altname, sizeof(mGlanceData.altname), "%s", mInfo.name);

                for (uint32_t i = 0; i < gl->controlsNumber; i++) {
                    LOG_DEBUG("ctrl %d, id %u, valid %d\n", i, gl->controls[i].id, gl->controls[i].valid);
                    mGlanceData.ctrls[i] = gl->controls[i];
                    mGlanceData.count++;
                    if(mGlanceData.count == App::kGlanceControlsMax) {
                        LOG_DEBUG("[%s] Maximum number of glance controls reached\n", mInfo.name);
                        break;
                    }
                }
            }

            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        case SDK::MessageType::REQUEST_WIDGET_START: {
            LOG_DEBUG("[%s] Widget start\n", mInfo.name);
            if (mLifeCycleCb && mBase) {
                mLifeCycleCb->onWidgetStart(mBase);
            }
            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        case SDK::MessageType::REQUEST_WIDGET_UPDATE: {
            auto* w = static_cast<SDK::Message::RequestWidgetUpdate*>(msg);
            // A POD over IPC cannot be trusted, so sanitize per the reader
            // contract: mask unknown/garbage bits, clamp the percent (the >= test
            // also maps NaN/-inf to 0), and bound the label copy. The %.*s caps
            // the read at the field size so an unterminated source cannot
            // over-read, and snprintf always terminates; a sender that fills all
            // WIDGET_TEXT_BYTES without a NUL loses its last byte, which may split
            // the trailing character -- bounded and safe, not repaired.
            App::WidgetContent content;
            content.shown   = w->shown & SDK::Message::WIDGET_SHOW_ALL;
            content.percent = (w->percent >= 0.0f) ? (w->percent > 100.0f ? 100.0f : w->percent) : 0.0f;
            snprintf(content.text, sizeof(content.text), "%.*s",
                     static_cast<int>(SDK::Message::WIDGET_TEXT_BYTES), w->text);
            if (mLifeCycleCb && mBase) {
                mLifeCycleCb->onWidgetUpdate(mBase, content);
            }
            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        case SDK::MessageType::REQUEST_WIDGET_STOP: {
            LOG_DEBUG("[%s] Widget stop\n", mInfo.name);
            if (mLifeCycleCb && mBase) {
                mLifeCycleCb->onWidgetStop(mBase);
            }
            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        default:
            break;
    }
}

void AppInstance::slMsgHandler(SDK::MessageBase* msg)
{
    if (!msg) {
        return;
    }

    switch (msg->getType()) {

        case SDK::MessageType::REQUEST_SENSOR_LAYER_GET_DEFAULT: {
            LOG_DEBUG("[%s] Requests get default sensor\n", mInfo.name);
            auto* req    = static_cast<SDK::Message::Sensor::RequestDefault*>(msg);
            auto* driver = Sensor::Manager::getInstance().getDefaultSensor(req->id);
            if (driver) {
                req->handle = driver->getHandle();
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
            } else {
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::FAIL);
            }
        } break;

        case SDK::MessageType::REQUEST_SENSOR_LAYER_GET_LIST: {
            LOG_DEBUG("[%s] Requests get sensor list\n", mInfo.name);
            auto* req  = static_cast<SDK::Message::Sensor::RequestList*>(msg);
            auto  list = Sensor::Manager::getInstance().getHandleList(req->id);
            if (list.size() <= ARRAY_SIZE(req->handles)) {
                req->handlesCount = list.size();
                for (uint8_t idx = 0; idx < list.size(); ++idx) {
                    req->handles[idx] = list[idx];
                }
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
            } else {
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::FAIL);
            }
        } break;

        case SDK::MessageType::REQUEST_SENSOR_LAYER_GET_DESCRIPTOR: {
            LOG_DEBUG("[%s] Requests get sensor description\n", mInfo.name);
            auto* req    = static_cast<SDK::Message::Sensor::RequestGetDesc*>(msg);
            auto* driver = Sensor::Manager::getInstance().getDriverByHandle(req->handle);
            if (driver) {
                strncpy(req->desc, driver->getDescription(), sizeof(req->desc));
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
            } else {
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::FAIL);
            }
        } break;

        case SDK::MessageType::REQUEST_SENSOR_LAYER_CONNECT: {
            LOG_DEBUG("[%s] Requests connect to sensor\n", mInfo.name);
            auto* req    = static_cast<SDK::Message::Sensor::RequestConnect*>(msg);
            auto* driver = Sensor::Manager::getInstance().getDriverByHandle(req->handle);

            SDK::Interface::ISensorDataListener* listener;
            if (msg->getProcessId() == mInfo.srvPID) {
                listener = &mSrvSensorListener;
            } else if (msg->getProcessId() == mInfo.guiPID) {
                listener = &mGuiSensorListener;
            } else {
                listener = nullptr;
            }

            if (driver && listener && driver->connect(listener, req->period, req->latency)) {
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
            } else {
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::FAIL);
            }
        } break;

        case SDK::MessageType::REQUEST_SENSOR_LAYER_DISCONNECT: {
            LOG_DEBUG("[%s] Requests disconnect from sensor\n", mInfo.name);
            auto* req    = static_cast<SDK::Message::Sensor::RequestDisconnect*>(msg);
            auto* driver = Sensor::Manager::getInstance().getDriverByHandle(req->handle);

            SDK::Interface::ISensorDataListener* listener;
            if (msg->getProcessId() == mInfo.srvPID) {
                listener = &mSrvSensorListener;
            } else if (msg->getProcessId() == mInfo.guiPID) {
                listener = &mGuiSensorListener;
            } else {
                listener = nullptr;
            }

            if (driver && listener) {
                driver->disconnect(listener);
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
            } else {
                mMessageMgr.signalCompletion(msg, SDK::MessageResult::FAIL);
            }
        } break;

        case SDK::MessageType::REQUEST_ACCESSORY_PREPARE: {
            auto* req = static_cast<SDK::Message::Accessory::RequestPrepare*>(msg);
            LOG_DEBUG("[%s] Requests accessory prepare 0x%08X\n",
                    mInfo.name, req->kinds);
            const bool ok = App::AccessoryManager::getInstance().prepare(
                    req->kinds, msg->getProcessId());
            mMessageMgr.signalCompletion(msg, ok ? SDK::MessageResult::SUCCESS
                                                 : SDK::MessageResult::FAIL);
        } break;

        case SDK::MessageType::REQUEST_ACCESSORY_RELEASE: {
            LOG_DEBUG("[%s] Requests accessory release\n", mInfo.name);
            App::AccessoryManager::getInstance().release(msg->getProcessId());
            mMessageMgr.signalCompletion(msg, SDK::MessageResult::SUCCESS);
        } break;

        default:
            break;
    }


}

} // namespace App
