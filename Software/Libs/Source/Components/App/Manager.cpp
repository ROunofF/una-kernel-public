/**
 ******************************************************************************
 * @file    AppManager.cpp
 * @brief
 ******************************************************************************
 *
 ******************************************************************************
 */

#include "App/Manager.hpp"
#include "App/Validator.hpp"
#include "App/AliasRules.hpp"
#include "App/IconPool.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include "SDK/Messages/CommandMessages.hpp"

#include "System.hpp"
#include "Crc/Crc32.hpp"
#include "AppSurface.hpp"
#include "HWButtons.hpp"
#include "SDK/JSON/JsonStreamWriter.hpp"
#include "SDK/Tools/FirmwareVersion.hpp"


#define LOG_MODULE_PRX      "App.Manager"
#define LOG_MODULE_LEVEL    LOG_LEVEL_INFO
#include "Logger.h"
#include <new>   // std::nothrow


namespace App
{

Manager::Manager()
        : mMessageManager{}
        , mApps {}
        , mLifeCycleCb(nullptr)
        , mWidgetMutex {}
        , mGuiTickTimer {}
        , mGlanceTickTimer {}
        , mAppLifecycleMutex {}
{
    mAppLifecycleMutex.init("AppLifecycle");
    mWidgetMutex.init("Widget");
    mGuiTickTimer.init("AppGuiTick", this, &App::Manager::guiTickTimCb, true, this);
    mGlanceTickTimer.init("AppGlanceTick", this, &App::Manager::glanceTickTimCb, true, this);
    HWButtons::GetInstance().attachCallback(this);
}

Manager::~Manager()
{

}

void Manager::scan()
{
    constexpr char skAppsPath[] = APPS_DIR_PATH;

    // Minimum file size
    static const uint32_t skIconSize = App::kIconSmallWH * App::kIconSmallWH;

    constexpr char skAppExtension[] = APP_FILE_EXTENSION;

    const uint32_t kDataBuffSize = 1024;
    struct Worker {
        FS::Directory appsDir;
        Interface::IFileSystem::ObjectInfo item;
        FS::Directory appDir;
        char path[FS::FileSystem::skMaxPathLen + 1];
        uint8_t buffer[kDataBuffSize];
    };

    Worker *w = new (std::nothrow) Worker;
    if (w == nullptr) {
        LOG_ERROR("No memory\n");
        return;
    }

    w->appsDir.setPath(skAppsPath);

    if (!w->appsDir.exist()) {
        w->appsDir.create();
        delete w;

        // There is no point in scanning a directory
        // if it has just been created
        return;
    }

    if (!w->appsDir.open()) {
        LOG_ERROR("Failed to open dir [%s]\n", skAppsPath);
        delete w;
        return;
    }

    // Take the registry's storage in one piece, before anything is added
    // to it. mApps holds App::Application by value at 632 bytes each, so
    // letting it double its way up copies every app registered so far and
    // leaves the old block behind: twenty apps walk capacities 1, 2, 4, 8,
    // 16, 32, so six allocations of which five copy, the last taking a
    // 20 KB block and copying 10 KB into it - at the point in boot where the
    // heap is being carved up for the rest of the run.
    //
    // An upper bound is what is wanted, not an exact count - a directory
    // may hold no app at all, or an alias that does not resolve. Reserving
    // too much costs some slack; reserving too little costs a reallocation,
    // which is the thing being removed.
    //
    // Counting means walking the directory twice, so it is rewound in place
    // with readNext(reset) - f_readdir(dir, NULL) - rather than closed and
    // reopened. That matters more than it looks: this is only an
    // optimisation, and a close/reopen pair would give app discovery two new
    // ways to fail outright. Losing every app at boot to save a realloc is a
    // bad trade, and "No Apps with the apps still on disk" is a symptom this
    // firmware has produced before.
    //
    // Close and reopen is kept as the fallback, so the worst case is the
    // failure the function already had before this block existed.
    {
        size_t appDirs = 0;

        while (w->appsDir.readNext(w->item)) {
            if (w->item.isDir) {
                ++appDirs;
            }
        }

        if (!w->appsDir.readNext(w->item, true)) {
            LOG_WARNING("Failed to rewind dir [%s]\n", skAppsPath);

            // close() leaves mIsOpened set when it fails, and ~Directory
            // would then close the same DIR again, so only reopen if it
            // actually closed.
            if (!w->appsDir.close() || !w->appsDir.open()) {
                LOG_ERROR("Failed to reopen dir [%s]\n", skAppsPath);
                delete w;
                return;
            }
        }

        // Capped, because appDirs counts directories rather than apps and
        // /Apps is writable over USB. An Application is 632 bytes by value
        // and a FAT directory can hold tens of thousands of entries, so
        // reserving the raw count lets a filesystem full of junk directories
        // exhaust the heap - ten thousand empty ones would ask for 6.3 MB to
        // hold possibly zero valid apps. A failed allocation here aborts,
        // which on this target is a reset, at boot, before anything has been
        // validated. Nothing could do that before: the vector only ever grew
        // for apps that passed validation.
        //
        // What the cap buys is precisely that the reservation is
        // SPECULATIVE while growth is not: reserve(appDirs) pays for
        // candidates that may never become apps, whereas doubling only ever
        // grows for elements actually pushed. It is not a smaller peak - past
        // the cap the vector doubles, so a count just above it takes a larger
        // single block than the exact reservation would have. It is a limit
        // on paying for directories that are not apps.
        // On an initially empty registry, growing beyond 64 entries requires
        // more than 64 unique, validated app or alias insertions; directory
        // count alone only affects this speculative reservation, not growth.
        //
        // This is not a limit on how many apps may be installed: the vector
        // still grows normally past the cap. The watch this was measured on
        // has 22 directories in /Apps.
        //
        // Total, not additional - scan() runs once with mApps empty today,
        // but say what is meant so a second call cannot quietly undo this.
        static constexpr size_t skMaxReservedApps = 64;

        mApps.reserve(mApps.size() + std::min(appDirs, skMaxReservedApps));
    }

    // Variant aliases found during the directory walk. Registered in a second
    // phase so every possible target is already in mApps when an alias
    // resolves.
    std::vector<std::string> aliasPaths;

    // Reads the 48-byte MainHeader of a candidate so PickUappInDir can route
    // real apps and aliases; addApp/addAlias re-read it during validation.
    // Heap the FS::File -- like the Worker above, it is too big for this
    // thread's stack (path buffer + FatFs FIL sector buffer).
    auto readHeader = [](const char *path, App::MainHeader &header) {
        std::unique_ptr<FS::File> f(new (std::nothrow) FS::File(path));
        return f && Loader::ReadMainHeader(*f, header);
    };

    while (w->appsDir.readNext(w->item)) {

        if (!w->item.isDir) {
            // Skip files
            continue;
        }

        if (strlen(skAppsPath) + strlen(w->item.name) >= sizeof(w->path)) {
            // Skip very long names
            LOG_WARNING("Long name [%s]\n", w->item.name);
            continue;
        }
        snprintf(w->path, FS::FileSystem::skMaxPathLen,
                "%s%s/", skAppsPath, w->item.name);

        w->appDir.setPath(w->path);
        if (!w->appDir.open()) {
            LOG_WARNING("Failed to open dir [%s]\n", w->path);
            continue;
        }

        App::DirPick pick = App::PickUappInDir(w->appDir, w->appDir.getPath(),
                skAppExtension, skIconSize, readHeader,
                w->path, sizeof(w->path));

        if (pick == App::DirPick::kRealApp) {
            std::shared_ptr<Interface::IFile> f = std::make_shared<FS::File>(w->path);
            if (addApp(f)) {
                LOG_INFO("Found app: [%s]\n", w->path);
            }
        } else if (pick == App::DirPick::kAlias) {
            aliasPaths.emplace_back(w->path);
        }

        w->appDir.close();

    } // while apps dir
    w->appsDir.close();
    delete w;

    // Phase 2: register the aliases now that all targets are known
    for (const auto &path : aliasPaths) {
        std::shared_ptr<Interface::IFile> f = std::make_shared<FS::File>(path.c_str());
        if (addAlias(f)) {
            LOG_INFO("Found variant: [%s]\n", path.c_str());
        }
    }

   // Add apps to the registry
    addAppsToRegistry();

}

bool Manager::addApp(std::shared_ptr<Interface::IFile> f)
{
    // Validate file content
    App::Validator validator(Driver::System::GetLibcVersion().u32);
    if (!validator.appIsValid(*f)){
        LOG_ERROR("App is invalid: [%s]\n", f->getPath());
        return false;
    }

    App::MainHeader mainHeader {};
    Info info {};

    if (!f->open()) {
        return false;
    }

    size_t br;
    bool succsess = f->read(reinterpret_cast<char*>(&mainHeader), sizeof(App::MainHeader), br);
    f->close();
    if (!succsess) {
        return false;
    }

    uint32_t pathCRC = CRC32::calculate(reinterpret_cast<const uint8_t*>(f->getPath()), strlen(f->getPath()));


    info.appID = mainHeader.uappID;
    info.version = mainHeader.uappVersion;

    info.srvPID = (0xFFFFFFF0 & pathCRC);
    info.guiPID = info.srvPID + 1;

    info.srvSize = mainHeader.serviceSize;
    info.guiSize = f->size() - sizeof(MainHeader) - mainHeader.normal_icon_size - mainHeader.small_icon_size - mainHeader.serviceSize;

    info.flags = mainHeader.flags;
    snprintf(info.name, sizeof(info.name), "%s", mainHeader.app_name);
    info.file = f;

    if (!appExists(info)) {
        mApps.emplace_back(info,
                mMessageManager,
                this,
                AppSurface::GetInstance(),
                mAppLifecycleMutex);
    } else {
        LOG_WARNING("App with appID=%llu already exists\n", info.appID);
    }

    return true;
}

bool Manager::addAlias(std::shared_ptr<Interface::IFile> f)
{
    // Validate file content. The libc version passed here is irrelevant to
    // aliases (they carry libcVersion = 0 and aliasIsValid never checks it);
    // ABI compatibility is the target binary's property.
    App::Validator validator(Driver::System::GetLibcVersion().u32);
    if (!validator.aliasIsValid(*f)) {
        LOG_ERROR("Variant alias is invalid: [%s]\n", f->getPath());
        return false;
    }

    App::MainHeader mainHeader {};
    App::VariantAliasPayload payload {};

    if (!Loader::ReadMainHeader(*f, mainHeader) ||
            !App::ReadAliasPayload(*f, payload)) {
        return false;
    }

    // Resolve the target over everything phase 1 registered
    std::vector<App::AliasTargetView> targets;
    targets.reserve(mApps.size());
    for (const auto &app : mApps) {
        const Info &i = app.getInfo();
        targets.push_back({i.appID, i.version, i.flags, i.isVariant});
    }

    App::AliasResolution res = App::ResolveAlias(mainHeader, payload,
            targets.data(), targets.size());

    if (res.status == App::AliasResolution::Status::kNoTarget) {
        LOG_WARNING("No target app %08X%08X for variant [%s]\n",
                static_cast<uint32_t>(payload.targetAppID >> 32),
                static_cast<uint32_t>(payload.targetAppID),
                f->getPath());
        return false;
    }

    if (res.status == App::AliasResolution::Status::kTargetTooOld) {
        LOG_WARNING("Target app is older than variant requires (0x%08X): [%s]\n",
                payload.minTargetVersion, f->getPath());
        return false;
    }

    if (res.status == App::AliasResolution::Status::kIdCollision) {
        LOG_WARNING("Variant uappID is already registered: [%s]\n", f->getPath());
        return false;
    }

    if (res.status == App::AliasResolution::Status::kGlanceTarget) {
        LOG_WARNING("Variants of glance apps are not supported: [%s]\n", f->getPath());
        return false;
    }

    if (res.typeAdjusted) {
        LOG_WARNING("Variant type differs from its target's; using the target's: [%s]\n",
                f->getPath());
    }

    // Copy what we need from the target before emplace_back can reallocate
    const Info &targetInfo = mApps[res.targetIndex].getInfo();
    std::shared_ptr<Interface::IFile> targetFile = targetInfo.file;

    Info info {};

    info.appID = mainHeader.uappID;
    info.version = mainHeader.uappVersion;

    uint32_t pathCRC = CRC32::calculate(reinterpret_cast<const uint8_t*>(f->getPath()),
            strlen(f->getPath()));
    info.srvPID = (0xFFFFFFF0 & pathCRC);
    info.guiPID = info.srvPID + 1;

    info.srvSize = targetInfo.srvSize;
    info.guiSize = targetInfo.guiSize;

    info.flags = res.flags;
    snprintf(info.name, sizeof(info.name), "%s", mainHeader.app_name);

    // Identity stays with the alias (sandbox, PIDs, icons, registry path);
    // code loads from the target through the variant's own file instance so
    // open/seek state can never be shared between entries.
    info.file = f;
    info.codeFile = std::make_shared<FS::File>(targetFile->getPath());
    info.isVariant = true;
    info.targetAppID = payload.targetAppID;

    if (!appExists(info)) {
        mApps.emplace_back(info,
                mMessageManager,
                this,
                AppSurface::GetInstance(),
                mAppLifecycleMutex);
    } else {
        LOG_WARNING("App with appID=%llu already exists\n", info.appID);
    }

    return true;
}

bool Manager::addAppsToRegistry()
{
    size_t listSize = mApps.size();

    FS::File f(APPS_LIST_FILE_PATH);

    if (!f.open(true, true)) {
        return false;
    }

    // +1 so the buffer is never zero-sized: with no apps we still write a
    // valid empty manifest (FA_CREATE_ALWAYS truncated the file on open)
    // instead of leaving the previous, now-stale contents.
    size_t buffSize = (listSize + 1) * 320;
    char *buff = new (std::nothrow)char[buffSize]; // TODO: fix JsonStreamWriter to work with file
    if (buff == nullptr) {
        // JsonStreamWriter no-ops on a null buffer without flagging an error,
        // so guard here to avoid a later strlen(nullptr) on the write path.
        LOG_ERROR("No memory for registry buffer\n");
        f.close();
        return false;
    }
    SDK::JsonStreamWriter w {buff, buffSize};

    auto swap_u64 = [](uint64_t v) {
        return ((v & 0x00000000000000FFULL) << 56) |
               ((v & 0x000000000000FF00ULL) << 40) |
               ((v & 0x0000000000FF0000ULL) << 24) |
               ((v & 0x00000000FF000000ULL) << 8)  |
               ((v & 0x000000FF00000000ULL) >> 8)  |
               ((v & 0x0000FF0000000000ULL) >> 24) |
               ((v & 0x00FF000000000000ULL) >> 40) |
               ((v & 0xFF00000000000000ULL) >> 56);
    };

    w.startMap();
    // Registry schema: 2 = variant-capable kernel. The phone keys variant
    // creation on this marker; old phones ignore the unknown field.
    w.add("schema", static_cast<uint32_t>(2));
    w.startArray("apps");
    for (auto &app : mApps) {
        {
            SDK::JsonStreamWriter::MapScope m{w};

            uint64_t id = swap_u64(app.getAppID());

            w.addHexString("id", reinterpret_cast<uint8_t*>(&id), sizeof(id));

            SDK::FirmwareVersion v;
            char vBuff[32] {};
            v.u32 = app.getInfo().version;

            snprintf(vBuff, sizeof(vBuff), "%u.%u.%u", v.major, v.minor, v.patch);
            w.add("version", const_cast<const char *>(vBuff));
            w.add("file", (static_cast<const char *>(app.getInfo().file->getPath())) + strlen(BLE_ROOT_DIR)); // skip drive
            w.add("name", app.getName());
            w.add("type", static_cast<uint32_t>(app.getType()));

            if (app.getInfo().isVariant) {
                uint64_t target = swap_u64(app.getInfo().targetAppID);
                w.add("variant", true);
                w.addHexString("target", reinterpret_cast<uint8_t*>(&target), sizeof(target));
            }
        }
    }
    w.endArray();
    w.endMap();

    if (!w.isError()) {
        size_t bw = 0;
        f.write(buff, strlen(buff), bw);
    } else {
        LOG_ERROR("Failed to create JSON\n");
    }

    delete[] buff;

    f.close();

    return true;
}

Application* Manager::findByPID(uint32_t pid)
{
    for (auto &app : mApps) {
        if (app.getSrvPID() == pid || app.getGuiPID() == pid) {
            return &app;
        }
    }
    return nullptr;
}

Application* Manager::findWatchFace(uint64_t appId)
{
    if (appId == 0) {
        return nullptr;
    }

    for (auto &app : mApps) {
        if (app.getAppID() == appId && app.isType(App::Type::kClockface)) {
            return &app;
        }
    }
    return nullptr;
}

Application* Manager::findLoadedWatchFace()
{
    for (auto &app : mApps) {
        if (app.isType(App::Type::kClockface) && app.isGuiRun()) {
            return &app;
        }
    }
    return nullptr;
}

std::vector<Application*> Manager::findAllByType(App::Type t)
{
    std::vector<Application*> result;
    result.reserve(mApps.size()); // optimization to avoid multiple reallocs

    for (auto &app : mApps) {
        if (app.isType(t)) {
            result.push_back(&app);
        }
    }

    return result;
}

std::vector<Application*> Manager::findAllAutorun()
{
    std::vector<Application*> result;
    result.reserve(mApps.size());

    for (auto &app : mApps) {
        if (app.isAutorun()) {
            result.push_back(&app);
        }
    }

    return result;
}

std::vector<Application*> Manager::findAllActive()
{
    std::vector<Application*> result;
    result.reserve(mApps.size());

    for (auto &app : mApps) {
        if (app.isActive()) {
            result.push_back(&app);
        }
    }

    return result;
}

std::vector<Application*> Manager::findAll()
{
    std::vector<Application*> result;
    result.reserve(mApps.size());

    for (auto &app : mApps) {
        result.push_back(&app);
    }

    return result;
}


bool Manager::readIcon(const App::Application *app, bool normal,
                       uint8_t *dest, uint32_t destSize)
{
    if (dest == nullptr || destSize == 0) {
        return false;
    }

    app = contains(app);
    if (!app) {
        return false;
    }

    Interface::IFile* f = app->getInfo().file.get();

    if (!f->open()) {
        return false;
    }

    // Straight into the caller's buffer. This used to allocate one of its own
    // and hand it back, which put 4.5 KB of heap traffic on the icon path for
    // every set fetched -- at up to ten a second while a menu is scrolling --
    // and with -fno-exceptions a failed new aborts. The pixels now never touch
    // the heap at all.
    const bool ok = [&]() -> bool {
        App::MainHeader mainHeader{};
        size_t br;
        if (!f->read(reinterpret_cast<char*>(&mainHeader), sizeof(MainHeader), br)) {
            LOG_ERROR("Failed to read the file, [%s]\n", f->getPath());
            return false;
        }

        uint32_t offset = sizeof(App::MainHeader) + (normal ? 0 : mainHeader.normal_icon_size);
        uint32_t size = normal ? mainHeader.normal_icon_size : mainHeader.small_icon_size;

        // A file claiming an icon bigger than the format allows would otherwise
        // run off the end of the caller's buffer.
        if (size == 0 || size > destSize) {
            LOG_ERROR("Icon size %u will not fit %u bytes, [%s]\n",
                    static_cast<unsigned>(size), static_cast<unsigned>(destSize),
                    f->getPath());
            return false;
        }

        if (!f->seek(offset)) {
            LOG_ERROR("Failed to seek in the file, [%s]\n", f->getPath());
            return false;
        }

        if (!f->read(reinterpret_cast<char*>(dest), size, br) || br != size) {
            LOG_ERROR("Failed to read icon data from file, [%s]\n", f->getPath());
            return false;
        }

        return true;
    }();

    f->close();

    return ok;
}

bool Manager::isListable(const App::Application &app)
{
    // Skip apps whose .uapp file no longer exists on disk (e.g. removed over
    // BLE since the boot scan). The registry is only rebuilt by the boot scan,
    // so without this check a deleted app would linger in the launcher as a
    // ghost entry with no icon until the next reboot.
    const auto &appFile = app.getInfo().file;
    if (!appFile || !appFile->exist()) {
        return false;
    }

    // Same for a variant whose base app was deleted mid-session: its code is
    // gone, so hide it like a deleted app.
    const auto &codeFile = app.getInfo().codeFile;
    if (codeFile && codeFile != appFile && !codeFile->exist()) {
        return false;
    }

    return true;
}

std::vector<uint64_t> Manager::getAppIds(App::Type t)
{
    std::vector<uint64_t> result;
    result.reserve(mApps.size());

    for (auto &app : mApps) {
        if (app.isType(t) && isListable(app)) {
            result.push_back(app.getAppID());
        }
    }

    return result;
}

std::vector<App::Item> Manager::getAppList(App::Type t)
{
    std::vector<App::Item> result;
    result.reserve(mApps.size());

    for (auto &app : mApps) {
        if (!app.isType(t)) {
            continue;
        }

        if (!isListable(app)) {
            continue;
        }

        App::Item& item = result.emplace_back();
        item.type       = t;
        item.appId      = app.getAppID();
        item.pidService = app.getSrvPID();
        item.pidGUI     = app.getGuiPID();
        snprintf(item.name, sizeof(item.name), "%s", app.getName());
        item.caps       = app.getCapabilities();
    }

    return result;
}

App::IconRef Manager::getIconSet(uint64_t appId)
{
    if (appId == 0) {
        return App::IconRef();
    }

    Application* app = nullptr;
    for (auto &candidate : mApps) {
        if (candidate.getAppID() == appId) {
            app = &candidate;
            break;
        }
    }

    if (app == nullptr) {
        return App::IconRef();
    }

    App::IconRef set = App::IconPool::take();
    if (!set) {
        // Every slot is being drawn from. The menu that asked draws this item
        // without an icon, which is what it was doing a moment ago anyway.
        LOG_WARNING("Icon pool exhausted [appId = 0x%016llX]\n",
                static_cast<unsigned long long>(appId));
        return App::IconRef();
    }

    App::IconSet& out = set.mutableSet();
    out.appId = appId;

    // Each icon is a seek and a read into the app's own file, so this is worth
    // asking for only for what is about to be drawn. A set with one icon
    // missing is still worth returning: the menu draws what it has, and the
    // slot was cleared on the way out so the rest is not the last tenant's.
    readIcon(app, true, out.icon.data(), static_cast<uint32_t>(out.icon.size()));
    readIcon(app, false, out.smallIcon.data(), static_cast<uint32_t>(out.smallIcon.size()));

    return set;
}

std::vector<App::GlanceData> Manager::getGlancesData()
{
    std::vector<App::GlanceData> result;
    uint32_t cnt = 0;
    for (auto &app : mApps) {
        if (!app.isType(App::Type::kGlance)) {
            continue;
        }
        cnt++;
    }
    result.reserve(cnt);

    for (auto &app : mApps) {
        if (!app.isType(App::Type::kGlance) || !app.isGlanceRun()) {
            continue;
        }
        App::GlanceData data = app.getGlanceData();

        // Skip empty objects
        if (data.count > 0) {
            result.push_back(data);
        }
    }

    return result;
}

void Manager::autoRun()
{
    LOG_INFO("Start all autorun applications\n");
    for (auto &app : mApps) {
        if (app.isAutorun()) {
            app.runService();
        }
    }
}

void Manager::stopAll()
{
    LOG_INFO("Terminate all active applications\n");
    for (auto &app : mApps) {
        app.stop(osWaitForever);
    }
}

// Glances are selected by base type, and the glance-interface flag is not
// consulted -- here or anywhere else in the kernel. It was packed into every
// .uapp regardless of type until SDK bb82a621, so an app carrying it may or may
// not serve glance data and nothing in the header tells the two apart. No app
// of another base type serves a glance today, so the claim is not merely
// untrustworthy, it has no true instances to honour.
//
// What honouring it would cost is worth knowing before anyone tries. A glance
// start loads the app's service and never its GUI, leaving the app to decide
// what to do when nothing wants it -- and that decision belongs to the app,
// which the header says nothing about. Every app carrying the legacy bit would
// be started on every visit to the glances screen and left to release itself by
// code that was never written for the case.
void Manager::startGlances()
{
    LOG_INFO("Start all glances\n");
    for (auto &app : mApps) {
        if (app.getType() == App::Type::kGlance) {
            app.runGlance();
        }
    }
}

// Every app rather than the one that reported: the reports are coalesced into
// one request (see Backend::onIdle), so any number of apps may have gone idle
// behind it. The walk is one short locked check per installed app.
void Manager::releaseIdleApps()
{
    for (auto &app : mApps) {
        app.releaseIfIdle();
    }
}

// Deliberately not the mirror of startGlances(): this only tells each app that
// its glance is off screen, and the app decides what that means for it. Exiting
// is the usual answer, but a glance that keeps a sensor subscription running is
// a legitimate one, so no service is unloaded here. The test matches
// startGlances() so the set signalled is the set started.
void Manager::stopGlances()
{
    LOG_INFO("Stop all glances\n");
    for (auto &app : mApps) {
        if (app.getType() == App::Type::kGlance) {
            app.stopGlance();
        }
    }
}

bool Manager::appExists(const Info &info) const {
    for (const auto &app : mApps) {
        const Info &i = app.getInfo();
        if (i.appID == info.appID &&
            i.srvPID == info.srvPID &&
            i.guiPID == info.guiPID)
        {
            return true;
        }
    }
    return false;
}

App::Application* Manager::contains(const App::Application* ptr)
{
    if (!ptr)
        return nullptr;

    for (Application& app : mApps) {
        if (&app == ptr)
            return &app;
    }

    return nullptr;
}



void Manager::guiTickTimCb(void *arg)
{
    uint32_t timestamp = OS::GetTimeMs();
    for (auto &app : mApps) {
        app.dispatchFrameTick(timestamp);
    }

    if (!isGuiTimerRequired() && mGuiTickTimer.isRunning()) {
        mGuiTickTimer.stop();
        LOG_DEBUG("Frame timer deactivated\n");
    }
}

void Manager::glanceTickTimCb(void *arg)
{
    uint32_t timestamp = OS::GetTimeMs();
    for (auto &app : mApps) {
        app.dispatchGlanceTick(timestamp);
    }

    if (!isGlanceTimerRequired() && mGlanceTickTimer.isRunning()) {
        mGlanceTickTimer.stop();
        LOG_DEBUG("Glance timer deactivated\n");
    }
}

bool Manager::isGuiTimerRequired()
{
    for (auto &app : mApps) {
        // Resumed, not merely loaded. An app that is not on screen has
        // nowhere to draw, and a watch face stays loaded for as long as the
        // watch is on -- pacing those would keep this running forever, and
        // with it the system out of deep idle.
        if (app.isGuiResumed()) {
            return true;
        }
    }
    return false;
}

bool Manager::isGlanceTimerRequired()
{
    for (auto &app : mApps) {
        if (app.isType(App::Type::kGlance) && app.isGlanceRun()) {
            return true;
        }
    }
    return false;
}

void Manager::onSrvStart(App::Application *app)
{
    if (mLifeCycleCb) {
        mLifeCycleCb->onSrvStart(app);
    }
}

void Manager::onSrvStop(App::Application *app)
{
    // Clear the home-widget slot if its owner's service is going away, so a dead
    // app leaves no stale element on the home screen. Under the lock so a dying
    // app cannot wipe a slot a newer owner already claimed.
    {
        OS::MutexCS cs(mWidgetMutex);
        if (app && app->getSrvPID() == mWidgetOwnerPid) {
            mWidgetOwnerPid = 0;
            if (mLifeCycleCb) {
                mLifeCycleCb->onWidgetStop(app);
            }
        }
    }

    if (mLifeCycleCb) {
        mLifeCycleCb->onSrvStop(app);
    }
}

void Manager::onWidgetStart(App::Application *app)
{
    if (!app) {
        return;
    }
    // Single slot, last-START-wins. Claim and forward under one lock so the GUI
    // event stream stays ordered against the ownership change.
    OS::MutexCS cs(mWidgetMutex);
    mWidgetOwnerPid = app->getSrvPID();
    if (mLifeCycleCb) {
        mLifeCycleCb->onWidgetStart(app);
    }
}

void Manager::onWidgetUpdate(App::Application *app, const App::WidgetContent &content)
{
    if (!app) {
        return;
    }
    // Only the current owner may drive the slot; check and forward under one lock
    // so a displaced app's stale update can never overtake the new owner's.
    OS::MutexCS cs(mWidgetMutex);
    if (app->getSrvPID() != mWidgetOwnerPid) {
        return;
    }
    if (mLifeCycleCb) {
        mLifeCycleCb->onWidgetUpdate(app, content);
    }
}

void Manager::onWidgetStop(App::Application *app)
{
    if (!app) {
        return;
    }
    // Release only while still the owner.
    OS::MutexCS cs(mWidgetMutex);
    if (app->getSrvPID() != mWidgetOwnerPid) {
        return;
    }
    mWidgetOwnerPid = 0;
    if (mLifeCycleCb) {
        mLifeCycleCb->onWidgetStop(app);
    }
}

void Manager::onGuiStart(App::Application *app)
{
    if (mLifeCycleCb) {
        mLifeCycleCb->onGuiStart(app);
    }
}

void Manager::onGuiStop(App::Application *app)
{
    // Ownership is by PID and a PID is derived from the app's path, so the
    // same app relaunching would otherwise be handed its own previous
    // session's last frame and show it until it draws. Only the app that holds
    // the surface gives it up -- background apps stop too.
    if (app) {
        AppSurface::GetInstance().releaseOwner(app->getGuiPID());
    }

    if (!isGuiTimerRequired() && mGuiTickTimer.isRunning()) {
        mGuiTickTimer.stop();
        LOG_DEBUG("Frame timer deactivated\n");
    }

    if (mLifeCycleCb) {
        mLifeCycleCb->onGuiStop(app);
    }
}

void Manager::onIdle(App::Application *app)
{
    if (mLifeCycleCb) {
        mLifeCycleCb->onIdle(app);
    }
}

void Manager::onGuiResume(App::Application *app)
{
    if (app) {
        AppSurface::GetInstance().setOwner(app->getGuiPID());

        if (!mGuiTickTimer.isRunning()) {
            LOG_DEBUG("Frame timer activated\n");
            mGuiTickTimer.start(1000 / App::kFrameRate);
        }
    }

    if (mLifeCycleCb) {
        mLifeCycleCb->onGuiResume(app);
    }
}

void Manager::onGuiSuspend(App::Application *app)
{
    // An app that has stood down has no more business drawing than a stopped
    // one, so it gives the surface up the same way. Without this the next
    // resume finds itself already the owner, which leaves the buffer holding
    // the frame it drew before it stood down -- a clock reading the time it
    // was put away. Only the app that holds the surface gives it up.
    if (app) {
        AppSurface::GetInstance().releaseOwner(app->getGuiPID());
    }

    if (!isGuiTimerRequired() && mGuiTickTimer.isRunning()) {
        mGuiTickTimer.stop();
        LOG_DEBUG("Frame timer deactivated\n");
    }

    if (mLifeCycleCb) {
        mLifeCycleCb->onGuiSuspend(app);
    }
}

void Manager::onGlanceRun(App::Application *app)
{
    if (!mGlanceTickTimer.isRunning()) {
        LOG_DEBUG("Glance timer activated\n");
        mGlanceTickTimer.start(1000/App::kGlanceRate);
    }

    if (mLifeCycleCb) {
        mLifeCycleCb->onGlanceRun(app);
    }
}

void Manager::onGlanceStop(App::Application *app)
{
    if (!isGlanceTimerRequired() && mGlanceTickTimer.isRunning()) {
        mGlanceTickTimer.stop();
        LOG_DEBUG("Glance timer deactivated\n");
    }

    if (mLifeCycleCb) {
        mLifeCycleCb->onGlanceStop(app);
    }
}

void Manager::onGuiStartFailed(App::Application *app)
{
    if (mLifeCycleCb) {
        mLifeCycleCb->onGuiStartFailed(app);
    }
}

void Manager::onGuiResumeRequest(App::Application *app)
{
    if (mLifeCycleCb) {
        mLifeCycleCb->onGuiResumeRequest(app);
    }
}

void Manager::onCapabilities(App::Application *app, const App::Capabilities &caps)
{
    if (mLifeCycleCb) {
        mLifeCycleCb->onCapabilities(app, caps);
    }
}

void Manager::onNewActivity(App::Application *app)
{
    if (mLifeCycleCb) {
        mLifeCycleCb->onNewActivity(app);
    }
}


void Manager::onPress(Interface::IButtons::Id id)
{
    uint32_t timestamp = OS::GetTimeMs();
    for (auto &app : mApps) {
        app.dispatchButton(timestamp,
                static_cast<SDK::Message::EventButton::Id>(id),
                SDK::Message::EventButton::Event::PRESS);
    }
}

void Manager::onRelease(Interface::IButtons::Id id)
{
    uint32_t timestamp = OS::GetTimeMs();
    for (auto &app : mApps) {
        app.dispatchButton(timestamp,
                static_cast<SDK::Message::EventButton::Id>(id),
                SDK::Message::EventButton::Event::RELEASE);
    }
}

void Manager::onClick(Interface::IButtons::Id id)
{
    uint32_t timestamp = OS::GetTimeMs();
    for (auto &app : mApps) {
        app.dispatchButton(timestamp,
                static_cast<SDK::Message::EventButton::Id>(id),
                SDK::Message::EventButton::Event::CLICK);
    }
}

void Manager::onLongPress(Interface::IButtons::Id id)
{
    uint32_t timestamp = OS::GetTimeMs();
    for (auto &app : mApps) {
        app.dispatchButton(timestamp,
                static_cast<SDK::Message::EventButton::Id>(id),
                SDK::Message::EventButton::Event::LONG_PRESS);
    }
}

void Manager::onHold_1s(Interface::IButtons::Id id)
{
    uint32_t timestamp = OS::GetTimeMs();
    for (auto &app : mApps) {
        app.dispatchButton(timestamp,
                static_cast<SDK::Message::EventButton::Id>(id),
                SDK::Message::EventButton::Event::HOLD_1S);
    }
}

void Manager::onHold_5s(Interface::IButtons::Id id)
{
    uint32_t timestamp = OS::GetTimeMs();
    for (auto &app : mApps) {
        app.dispatchButton(timestamp,
                static_cast<SDK::Message::EventButton::Id>(id),
                SDK::Message::EventButton::Event::HOLD_5S);
    }
}

void Manager::onHold_10s(Interface::IButtons::Id id)
{
    uint32_t timestamp = OS::GetTimeMs();
    for (auto &app : mApps) {
        app.dispatchButton(timestamp,
                static_cast<SDK::Message::EventButton::Id>(id),
                SDK::Message::EventButton::Event::HOLD_10S);
    }
}

} // namespace App
