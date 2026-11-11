#include <gtest/gtest.h>

#include <string>

#include "FileSystem/GuardPath.hpp"

// Pure path-policy tests for FileSystemGuard (FsGuard helper). These exercise
// the REAL guard rules — unlike the calibrator's in-memory fake filesystem,
// which is a different IFileSystem and does not model them. Regression cover
// for the on-device bug where the "../SharedData" directory was unresolvable
// (so it could never be mkdir'd and the calibrator never persisted anything).

namespace
{
constexpr char kRoot[] = "0:/apps/Running";   // trailing '/' already stripped

std::string full(const char *rel, const char *root = kRoot)
{
    char buf[FsGuard::kMaxPathLen] = {};
    return FsGuard::buildFullPath(root, rel, buf, sizeof(buf)) ? std::string(buf)
                                                               : std::string("<rejected>");
}

std::string rel(const char *fullPath, const char *root = kRoot)
{
    char buf[FsGuard::kMaxPathLen] = {};
    return FsGuard::buildRelativePath(root, fullPath, buf, sizeof(buf))
               ? std::string(buf) : std::string("<rejected>");
}
} // namespace

// --- The regression: the SharedData directory itself must resolve -----------

TEST(GuardPath, SharedDataDirectoryResolves)
{
    // Bare "../SharedData" (no trailing slash) is what callers mkdir before
    // opening a file in it. This was previously rejected as traversal.
    EXPECT_EQ(full("../SharedData"),  "0:/apps/SharedData");
    EXPECT_EQ(full("../SharedData/"), "0:/apps/SharedData");
}

TEST(GuardPath, SharedDataFilesResolve)
{
    EXPECT_EQ(full("../SharedData/stride.json"),      "0:/apps/SharedData/stride.json");
    EXPECT_EQ(full("../SharedData/stride_trace.csv"), "0:/apps/SharedData/stride_trace.csv");
    EXPECT_EQ(full("../SharedData/stride.json.bak"),  "0:/apps/SharedData/stride.json.bak");
}

TEST(GuardPath, SharedDataNameWithDotsAllowed)
{
    // ".." as a substring (not a whole segment) is a legitimate filename.
    EXPECT_EQ(full("../SharedData/log..txt"),    "0:/apps/SharedData/log..txt");
    EXPECT_EQ(full("../SharedData/sub/..hidden"),"0:/apps/SharedData/sub/..hidden");
}

TEST(GuardPath, SharedDataEscapeRejected)
{
    EXPECT_EQ(full("../SharedData/../secret"),       "<rejected>");
    EXPECT_EQ(full("../SharedData/../../etc/passwd"),"<rejected>");
    EXPECT_EQ(full("../SharedData/sub/../../x"),     "<rejected>");
}

// --- Normal in-root paths ---------------------------------------------------

TEST(GuardPath, NormalPathsResolveUnderRoot)
{
    EXPECT_EQ(full("log.txt"),       "0:/apps/Running/log.txt");
    EXPECT_EQ(full("sub/log.txt"),   "0:/apps/Running/sub/log.txt");
    EXPECT_EQ(full("/log.txt"),      "0:/apps/Running/log.txt");  // leading slash
    EXPECT_EQ(full("a..b/c..d.txt"), "0:/apps/Running/a..b/c..d.txt");
}

// --- Traversal rejection ----------------------------------------------------

TEST(GuardPath, ParentTraversalRejected)
{
    EXPECT_EQ(full(".."),         "<rejected>");
    EXPECT_EQ(full("../etc"),     "<rejected>");
    EXPECT_EQ(full("a/../../x"),  "<rejected>");
    EXPECT_EQ(full("a/.."),       "<rejected>");
    EXPECT_EQ(full("/../x"),      "<rejected>");
    EXPECT_EQ(full("../SekretData"), "<rejected>");  // not the SharedData dir
}

TEST(GuardPath, TruncationRejected)
{
    char small[8] = {};
    EXPECT_FALSE(FsGuard::buildFullPath(kRoot, "some/long/name.txt", small, sizeof(small)));
}

// --- Inverse mapping --------------------------------------------------------

TEST(GuardPath, RelativeRoundTrips)
{
    EXPECT_EQ(rel("0:/apps/Running/log.txt"),            "/log.txt");
    EXPECT_EQ(rel("0:/apps/SharedData/stride.json"),     "../SharedData/stride.json");
    EXPECT_EQ(rel("0:/apps/SharedData/stride_trace.csv"),"../SharedData/stride_trace.csv");
    // The SharedData directory itself must invert too (buildFullPath accepts it,
    // so DirectoryGuard("../SharedData").getPath() must not return nullptr).
    EXPECT_EQ(rel("0:/apps/SharedData"),                 "../SharedData");
}

TEST(GuardPath, RelativeRejectsSiblingSharingTextPrefix)
{
    // Root "0:/app" must NOT match "0:/apple/x" (boundary check).
    EXPECT_EQ(rel("0:/apple/x.txt", "0:/app"), "<rejected>");
    // Exact root or a non-child path is rejected.
    EXPECT_EQ(rel("0:/other/x.txt"), "<rejected>");
}

TEST(GuardPath, NullAndEmptyInputsRejected)
{
    char buf[FsGuard::kMaxPathLen] = {};
    // Boundary guards (callers assert non-null, but asserts are off in release).
    EXPECT_FALSE(FsGuard::buildFullPath(nullptr, "x", buf, sizeof(buf)));
    EXPECT_FALSE(FsGuard::buildFullPath(kRoot, nullptr, buf, sizeof(buf)));
    EXPECT_FALSE(FsGuard::buildFullPath(kRoot, "x", nullptr, sizeof(buf)));
    EXPECT_FALSE(FsGuard::buildFullPath(kRoot, "x", buf, 0));
    EXPECT_FALSE(FsGuard::buildRelativePath(nullptr, "0:/apps/Running/x", buf, sizeof(buf)));
    EXPECT_FALSE(FsGuard::buildRelativePath(kRoot, nullptr, buf, sizeof(buf)));
    EXPECT_FALSE(FsGuard::buildRelativePath(kRoot, "0:/apps/Running/x", nullptr, sizeof(buf)));
}

// --- Volume roots: ":/Workouts" -------------------------------------------

namespace
{
struct Resolved {
    std::string    path;
    FsGuard::Access access;
};

Resolved resolveVol(const char *relPath, bool volumeRoots = true, const char *root = kRoot)
{
    char buf[FsGuard::kMaxPathLen] = {};
    FsGuard::Access access = FsGuard::Access::App;
    const bool ok = FsGuard::resolve(root, relPath, volumeRoots, buf, sizeof(buf), access);
    return {ok ? std::string(buf) : std::string("<rejected>"), access};
}

std::string relVol(const char *fullPath, const char *root = kRoot)
{
    char buf[FsGuard::kMaxPathLen] = {};
    return FsGuard::buildRelativePath(root, fullPath, buf, sizeof(buf), true)
               ? std::string(buf) : std::string("<rejected>");
}
} // namespace

TEST(GuardPath, WorkoutsResolvesToTheVolumeRootReadOnly)
{
    for (const char *p : {":/Workouts", ":/Workouts/"}) {
        const Resolved r = resolveVol(p);
        EXPECT_EQ(r.path, "0:/Workouts") << p;
        EXPECT_EQ(r.access, FsGuard::Access::SharedReadOnly) << p;
    }
    const Resolved f = resolveVol(":/Workouts/Schedule/2026-10-09.fit");
    EXPECT_EQ(f.path, "0:/Workouts/Schedule/2026-10-09.fit");
    EXPECT_EQ(f.access, FsGuard::Access::SharedReadOnly);
    EXPECT_FALSE(FsGuard::canModify(f.access));
}

TEST(GuardPath, OtherAccessClasses)
{
    EXPECT_EQ(resolveVol("log.txt").access, FsGuard::Access::App);
    EXPECT_EQ(resolveVol("../SharedData/stride.json").access, FsGuard::Access::Shared);
    EXPECT_TRUE(FsGuard::canModify(FsGuard::Access::App));
    EXPECT_TRUE(FsGuard::canModify(FsGuard::Access::Shared));
}

TEST(GuardPath, OnlyListedVolumeRootsResolve)
{
    EXPECT_EQ(resolveVol(":/Apps").path,          "<rejected>");
    EXPECT_EQ(resolveVol(":/Apps/Other/x").path,  "<rejected>");
    EXPECT_EQ(resolveVol(":/WorkoutsX").path,     "<rejected>");  // not a whole name
    EXPECT_EQ(resolveVol(":/workouts").path,      "<rejected>");  // exact name only
    EXPECT_EQ(resolveVol(":/").path,              "<rejected>");
    EXPECT_EQ(resolveVol(":/settings.json").path, "<rejected>");
}

TEST(GuardPath, VolumeRootEscapeRejected)
{
    EXPECT_EQ(resolveVol(":/Workouts/../Apps/x").path,      "<rejected>");
    EXPECT_EQ(resolveVol(":/Workouts/a/../../x").path,      "<rejected>");
    EXPECT_EQ(resolveVol(":/../Apps/Running/x").path,       "<rejected>");
    EXPECT_EQ(resolveVol(":/Workouts/..").path,             "<rejected>");
    // ".." inside a name is not traversal.
    EXPECT_EQ(resolveVol(":/Workouts/a..b.fit").path, "0:/Workouts/a..b.fit");
}

TEST(GuardPath, WithoutVolumeRootsAColonPathIsUnchanged)
{
    // A guard that does not offer volume roots (e.g. the BLE file-transfer
    // guard) keeps the old meaning: an ordinary relative path, which FatFs
    // then rejects for the ':' in a name.
    const Resolved r = resolveVol(":/Workouts/a.fit", false);
    EXPECT_EQ(r.path, "0:/apps/Running/:/Workouts/a.fit");
    EXPECT_EQ(r.access, FsGuard::Access::App);
    EXPECT_EQ(full(":/Workouts/a.fit"), "0:/apps/Running/:/Workouts/a.fit");
}

TEST(GuardPath, SlashPathsAreUnchangedByVolumeRoots)
{
    // A leading "/" or "//" still means the app's own folder.
    EXPECT_EQ(resolveVol("/Workouts/a.fit").path,  "0:/apps/Running/Workouts/a.fit");
    EXPECT_EQ(resolveVol("//Workouts/a.fit").path, "0:/apps/Running//Workouts/a.fit");
    EXPECT_EQ(resolveVol("/Workouts/a.fit").access, FsGuard::Access::App);
}

TEST(GuardPath, FileTransferRootIsUnchanged)
{
    // The BLE file-transfer guard is rooted at "2:" without volume roots: its
    // "/Workouts/a.fit" is the shared folder itself, and writable.
    const Resolved r = resolveVol("/Workouts/a.fit", false, "2:");
    EXPECT_EQ(r.path, "2:/Workouts/a.fit");
    EXPECT_EQ(r.access, FsGuard::Access::App);
    EXPECT_EQ(resolveVol(":/Workouts/a.fit", false, "2:").path, "2:/:/Workouts/a.fit");
}

TEST(GuardPath, VolumeRootsNeedAVolumeInTheRoot)
{
    EXPECT_EQ(resolveVol(":/Workouts", true, "/no/volume").path, "<rejected>");
}

TEST(GuardPath, VolumeRootRelativeRoundTrips)
{
    EXPECT_EQ(relVol("0:/Workouts"),                ":/Workouts");
    EXPECT_EQ(relVol("0:/Workouts/a.fit"),          ":/Workouts/a.fit");
    EXPECT_EQ(relVol("0:/Workouts/Schedule/b.fit"), ":/Workouts/Schedule/b.fit");
    EXPECT_EQ(relVol("0:/WorkoutsX/a.fit"),         "<rejected>");  // not under root either
    // App and SharedData paths still invert as before.
    EXPECT_EQ(relVol("0:/apps/Running/log.txt"),        "/log.txt");
    EXPECT_EQ(relVol("0:/apps/SharedData/stride.json"), "../SharedData/stride.json");
    // Without volume roots the shared folder is outside the root.
    EXPECT_EQ(rel("0:/Workouts/a.fit"), "<rejected>");
}

TEST(GuardPath, VolumeRootTruncationRejected)
{
    char small[12] = {};
    FsGuard::Access access = FsGuard::Access::App;
    EXPECT_FALSE(FsGuard::resolve(kRoot, ":/Workouts/long-name.fit", true, small, sizeof(small),
                                  access));
}

TEST(GuardPath, HasParentSegment)
{
    EXPECT_TRUE(FsGuard::hasParentSegment(".."));
    EXPECT_TRUE(FsGuard::hasParentSegment("../x"));
    EXPECT_TRUE(FsGuard::hasParentSegment("a/../b"));
    EXPECT_TRUE(FsGuard::hasParentSegment("a/.."));
    EXPECT_FALSE(FsGuard::hasParentSegment("log..txt"));
    EXPECT_FALSE(FsGuard::hasParentSegment("sub/..hidden"));
    EXPECT_FALSE(FsGuard::hasParentSegment("normal/path.txt"));
}
