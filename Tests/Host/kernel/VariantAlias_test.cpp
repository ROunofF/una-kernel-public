/**
 * Unit tests for the variant-alias mechanism:
 *  - Validator::aliasIsValid / appIsValid against hand-crafted .uapp byte fixtures
 *  - ResolveAlias target resolution and flag sanitisation
 *  - PickUappInDir mixed-directory determinism
 *
 * Fixtures are built byte-for-byte to the on-disk layout so these tests pin
 * the file-format contract shared with make_variant.py and the phone.
 */

#include "App/AliasRules.hpp"
#include "App/AppHeaders.hpp"
#include "App/Validator.hpp"
#include "Crc/Crc32.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kLibcVersion = 0x00000002;

// ---------------------------------------------------------------------------
// In-memory filesystem fakes
// ---------------------------------------------------------------------------

class MemFile : public Interface::IFile {
public:
    MemFile(std::string path, std::vector<uint8_t> data)
        : mPath(std::move(path)), mData(std::move(data)) {}

    // IFsObject
    void setPath(const char *path) override { mPath = path; }
    const char* getPath() const override { return mPath.c_str(); }
    bool exist() const override { return true; }
    bool rename(const char *) override { return false; }
    bool remove() override { return false; }

    // IFile
    size_t size() const override { return mData.size(); }
    bool open(bool = false, bool = false) override { mOpen = true; mPos = 0; return true; }
    bool isOpen() const override { return mOpen; }
    bool close() override { mOpen = false; return true; }

    bool read(char *buff, size_t btr, size_t &br) override
    {
        if (!mOpen || mPos + btr > mData.size()) {
            br = 0;
            return false;
        }
        std::memcpy(buff, mData.data() + mPos, btr);
        mPos += btr;
        br = btr;
        return true;
    }

    bool write(const char *, size_t, size_t &bw) override { bw = 0; return false; }
    bool seek(size_t offset) override
    {
        if (offset > mData.size()) {
            return false;
        }
        mPos = offset;
        return true;
    }
    bool truncate(size_t) override { return false; }
    bool flush() override { return true; }
    size_t getPosition() const override { return mPos; }

private:
    std::string mPath;
    std::vector<uint8_t> mData;
    size_t mPos = 0;
    bool mOpen = false;
};

struct FakeEntry {
    std::string name;
    bool isDir = false;
    size_t size = 0;
};

class MemDirectory : public Interface::IDirectory {
public:
    explicit MemDirectory(std::vector<FakeEntry> entries)
        : mEntries(std::move(entries)) {}

    // IFsObject
    void setPath(const char *path) override { mPath = path; }
    const char* getPath() const override { return mPath.c_str(); }
    bool exist() const override { return true; }
    bool rename(const char *) override { return false; }
    bool remove() override { return false; }

    // IDirectory
    bool create() override { return true; }
    bool open() override { mIdx = 0; mOpen = true; return true; }
    bool isOpen() const override { return mOpen; }
    bool close() override { mOpen = false; return true; }

    bool readNext(Interface::IFileSystem::ObjectInfo &item, bool reset = false) override
    {
        if (reset) {
            mIdx = 0;
        }
        if (mIdx >= mEntries.size()) {
            return false;
        }
        const FakeEntry &e = mEntries[mIdx++];
        std::snprintf(item.name, sizeof(item.name), "%s", e.name.c_str());
        item.isDir = e.isDir;
        item.isHidden = false;
        item.isSystem = false;
        item.size = e.size;
        item.utc = 0;
        return true;
    }

private:
    std::vector<FakeEntry> mEntries;
    std::string mPath = "2:/Apps/Test/";
    size_t mIdx = 0;
    bool mOpen = false;
};

// ---------------------------------------------------------------------------
// Byte-level fixture builders
// ---------------------------------------------------------------------------

void append(std::vector<uint8_t> &out, const void *data, size_t len)
{
    const uint8_t *p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + len);
}

void appendCrc(std::vector<uint8_t> &out)
{
    uint32_t crc = CRC32::calculate(out.data(), static_cast<uint32_t>(out.size()));
    append(out, &crc, sizeof(crc));
}

struct AliasOpts {
    const char *name = "Walking";
    uint64_t uappID = 0xAA00000000000001ULL;
    uint32_t version = 1;
    uint32_t flags = FLAG_APP_VARIANT_ALIAS;  // type bits 0 = activity
    uint32_t serviceSize = 0;
    uint32_t normalIconSize = App::kIconNormalBytes;
    uint32_t smallIconSize = App::kIconSmallBytes;
    uint32_t payloadVersion = App::kAliasPayloadVersion;
    uint64_t targetAppID = 0xBB00000000000002ULL;
    uint32_t minTargetVersion = 0;
    uint8_t  origin = 0;
    std::string config = "{\"schema\":1,\"name\":\"Walking\"}";
    // When >= 0, overrides the payload's configSize field (size-lie fixtures).
    int32_t configSizeOverride = -1;
    bool corruptCrc = false;
    bool unterminatedName = false;
};

std::vector<uint8_t> buildAlias(const AliasOpts &o)
{
    App::MainHeader h {};
    h.uappID = o.uappID;
    h.uappVersion = o.version;
    h.libcVersion = 0;
    h.serviceSize = o.serviceSize;
    h.flags = o.flags;
    std::snprintf(h.app_name, sizeof(h.app_name), "%s", o.name);
    if (o.unterminatedName) {
        std::memset(h.app_name, 'X', sizeof(h.app_name));
    }
    h.normal_icon_size = o.normalIconSize;
    h.small_icon_size = o.smallIconSize;

    App::VariantAliasPayload p {};
    p.payloadVersion = o.payloadVersion;
    p.targetAppID = o.targetAppID;
    p.minTargetVersion = o.minTargetVersion;
    p.origin = o.origin;
    p.configSize = (o.configSizeOverride >= 0)
            ? static_cast<uint32_t>(o.configSizeOverride)
            : static_cast<uint32_t>(o.config.size());

    std::vector<uint8_t> out;
    append(out, &h, sizeof(h));
    out.insert(out.end(), App::kIconNormalBytes + App::kIconSmallBytes, 0x11);
    append(out, &p, sizeof(p));
    append(out, o.config.data(), o.config.size());
    appendCrc(out);

    if (o.corruptCrc) {
        out.back() ^= 0xFF;
    }

    return out;
}

std::vector<uint8_t> buildRealApp(const char *name,
                                  uint64_t uappID,
                                  uint32_t libcVersion = kLibcVersion,
                                  uint32_t flags = 0)
{
    App::MainHeader h {};
    h.uappID = uappID;
    h.uappVersion = 1;
    h.libcVersion = libcVersion;
    h.serviceSize = sizeof(App::UappHeader);
    h.flags = flags;
    std::snprintf(h.app_name, sizeof(h.app_name), "%s", name);
    h.normal_icon_size = App::kIconNormalBytes;
    h.small_icon_size = App::kIconSmallBytes;

    App::UappHeader service {};
    service.magic[0] = 'U';
    service.magic[1] = 'A';
    service.magic[2] = 'P';
    service.magic[3] = 'P';

    std::vector<uint8_t> out;
    append(out, &h, sizeof(h));
    out.insert(out.end(), App::kIconNormalBytes + App::kIconSmallBytes, 0x22);
    append(out, &service, sizeof(service));
    appendCrc(out);

    return out;
}

// ---------------------------------------------------------------------------
// On-disk layout contract
// ---------------------------------------------------------------------------

// Golden fixture built at EXPLICIT byte offsets (never through the C++
// structs) with a CRC32 literal computed independently (Python zlib.crc32,
// the same routine make_variant.py uses). This is the tripwire that fails if
// anyone reorders a struct field, changes the packing, or diverges the
// kernel CRC from the packer's: the struct-based fixtures elsewhere in this
// file would happily stay self-consistent through such a change.
TEST(AliasLayout, GoldenBytesLockTheFormat)
{
    std::vector<uint8_t> g;
    auto u64 = [&g](uint64_t v) { for (int i = 0; i < 8; i++) g.push_back(uint8_t(v >> (8 * i))); };
    auto u32 = [&g](uint32_t v) { for (int i = 0; i < 4; i++) g.push_back(uint8_t(v >> (8 * i))); };

    // MainHeader, field by field at its documented offset
    u64(0x1122334455667788ULL);              // 0  uappID
    u32(0x00010203);                         // 8  uappVersion
    u32(0);                                  // 12 libcVersion
    u32(0);                                  // 16 serviceSize
    u32(0x40);                               // 20 flags = FLAG_APP_VARIANT_ALIAS
    const char name[16] = "Golden";          // 24 app_name (NUL-padded)
    append(g, name, 16);
    u32(3600);                               // 40 normal_icon_size
    u32(900);                                // 44 small_icon_size
    ASSERT_EQ(g.size(), 48u);
    g.insert(g.end(), 4500, 0xA5);           // both icons
    // VariantAliasPayload at 4548
    u32(1);                                  // +0  payloadVersion
    u64(0x99AABBCCDDEEF000ULL);              // +4  targetAppID
    u32(0x00010000);                         // +12 minTargetVersion
    g.push_back(1);                          // +16 origin = user
    u32(12);                                 // +17 configSize
    g.insert(g.end(), 11, 0x00);             // +21 reserved
    ASSERT_EQ(g.size(), App::kAliasPayloadOffset + 32u);
    const char config[] = "{\"schema\":1}";  // 4580, 12 bytes
    append(g, config, 12);

    // The kernel's CRC must equal the packer's (zlib) for these exact bytes.
    EXPECT_EQ(CRC32::calculate(g.data(), static_cast<uint32_t>(g.size())), 0x7C473C23u);
    u32(0x7C473C23);                         // trailing CRC
    ASSERT_EQ(g.size(), App::kAliasFixedSize + 12u);

    MemFile f("2:/Apps/Golden/Golden.uapp", g);
    App::Validator v(kLibcVersion);
    EXPECT_TRUE(v.aliasIsValid(f));

    App::VariantAliasPayload p {};
    ASSERT_TRUE(App::ReadAliasPayload(f, p));
    EXPECT_EQ(p.payloadVersion, 1u);
    EXPECT_EQ(p.targetAppID, 0x99AABBCCDDEEF000ULL);
    EXPECT_EQ(p.minTargetVersion, 0x00010000u);
    EXPECT_EQ(p.origin, 1u);
    EXPECT_EQ(p.configSize, 12u);

    App::MainHeader h {};
    std::memcpy(&h, g.data(), sizeof(h));
    EXPECT_EQ(h.uappID, 0x1122334455667788ULL);
    EXPECT_EQ(h.uappVersion, 0x00010203u);
    EXPECT_STREQ(h.app_name, "Golden");
    EXPECT_TRUE(App::IsAliasHeader(h));
}

TEST(AliasLayout, MatchesDesignDocOffsets)
{
    EXPECT_EQ(sizeof(App::MainHeader), 48u);
    EXPECT_EQ(sizeof(App::VariantAliasPayload), 32u);
    EXPECT_EQ(App::kAliasPayloadOffset, 4548u);
    EXPECT_EQ(App::kAliasFixedSize, 4584u);
    EXPECT_EQ(App::kAliasConfigSizeMax, 8192u);
}

TEST(AliasLayout, FixtureSizeIsFixedPlusConfig)
{
    AliasOpts o;
    EXPECT_EQ(buildAlias(o).size(), App::kAliasFixedSize + o.config.size());
}

// ---------------------------------------------------------------------------
// Validator
// ---------------------------------------------------------------------------

TEST(AliasValidator, RealAppPasses)
{
    MemFile f("2:/Apps/Hiking/Hiking.uapp", buildRealApp("Hiking", 7));
    App::Validator v(kLibcVersion);
    EXPECT_TRUE(v.appIsValid(f));
}

TEST(AliasValidator, RealAppWrongLibcRejected)
{
    MemFile f("2:/Apps/Hiking/Hiking.uapp",
            buildRealApp("Hiking", 7, kLibcVersion + 1));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.appIsValid(f));
}

// The old-kernel back-compat lock: the shipped app validator must reject an
// alias both via the libc check (libcVersion = 0) and the flags mask (0x40).
TEST(AliasValidator, AppValidatorRejectsAlias)
{
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(AliasOpts{}));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.appIsValid(f));
}

TEST(AliasValidator, AliasValidatorRejectsRealApp)
{
    MemFile f("2:/Apps/Hiking/Hiking.uapp", buildRealApp("Hiking", 7));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

TEST(AliasValidator, ValidAliasPasses)
{
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(AliasOpts{}));
    App::Validator v(kLibcVersion);
    EXPECT_TRUE(v.aliasIsValid(f));
}

TEST(AliasValidator, EmptyConfigAllowed)
{
    AliasOpts o;
    o.config.clear();
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_TRUE(v.aliasIsValid(f));
}

TEST(AliasValidator, BadCrcRejected)
{
    AliasOpts o;
    o.corruptCrc = true;
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

TEST(AliasValidator, MissingAliasFlagRejected)
{
    AliasOpts o;
    o.flags = 0;  // alias-shaped file without the discriminator
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

TEST(AliasValidator, ExtraFlagsRejected)
{
    AliasOpts o;
    o.flags = FLAG_APP_VARIANT_ALIAS | 0x80;
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

// The accepted masks pin what kernels already in the field do with the
// unassigned bits: 0x04 rejects the image, 0x10 is accepted and ignored.
// Moving either bit between the two changes which new flags old kernels load.
TEST(AliasValidator, AcceptedMasksMatchTheFieldValidator)
{
    EXPECT_EQ(FLAG_APP_ACCEPTED_MASK, 0x3Bu);
    EXPECT_EQ(FLAG_ALIAS_ACCEPTED_MASK, 0x7Bu);
}

TEST(AliasValidator, EveryAppTypeAccepted)
{
    App::Validator v(kLibcVersion);
    for (uint32_t type = 0; type <= FLAG_APP_TYPE_MASK; ++type) {
        MemFile app("2:/Apps/Hiking/Hiking.uapp", buildRealApp("Hiking", 7, kLibcVersion, type));
        EXPECT_TRUE(v.appIsValid(app)) << "type " << type;

        AliasOpts o;
        o.flags = FLAG_APP_VARIANT_ALIAS | type;
        MemFile alias("2:/Apps/Walking/Walking.uapp", buildAlias(o));
        EXPECT_TRUE(v.aliasIsValid(alias)) << "type " << type;
    }
}

TEST(AliasValidator, ReservedMustBeZeroBitRejected)
{
    App::Validator v(kLibcVersion);
    MemFile app("2:/Apps/Hiking/Hiking.uapp", buildRealApp("Hiking", 7, kLibcVersion, 0x04));
    EXPECT_FALSE(v.appIsValid(app));

    AliasOpts o;
    o.flags = FLAG_APP_VARIANT_ALIAS | 0x04;
    MemFile alias("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    EXPECT_FALSE(v.aliasIsValid(alias));
}

TEST(AliasValidator, ReservedIgnoredBitAccepted)
{
    App::Validator v(kLibcVersion);
    MemFile app("2:/Apps/Hiking/Hiking.uapp",
            buildRealApp("Hiking", 7, kLibcVersion, FLAG_APP_RESERVED_IGNORED));
    EXPECT_TRUE(v.appIsValid(app));

    AliasOpts o;
    o.flags = FLAG_APP_VARIANT_ALIAS | FLAG_APP_RESERVED_IGNORED;
    MemFile alias("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    EXPECT_TRUE(v.aliasIsValid(alias));
}

TEST(AliasValidator, NonZeroServiceSizeRejected)
{
    AliasOpts o;
    o.serviceSize = 64;
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

TEST(AliasValidator, UnknownPayloadVersionRejected)
{
    AliasOpts o;
    o.payloadVersion = 2;
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

TEST(AliasValidator, ConfigSizeBoundRejected)
{
    AliasOpts o;
    o.config.assign(App::kAliasConfigSizeMax + 1, 'x');
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

TEST(AliasValidator, ConfigSizeLieRejected)
{
    AliasOpts o;
    o.configSizeOverride = static_cast<int32_t>(o.config.size()) + 8;
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

TEST(AliasValidator, UnterminatedNameRejected)
{
    AliasOpts o;
    o.unterminatedName = true;
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

TEST(AliasValidator, WrongIconSizeRejected)
{
    AliasOpts o;
    o.normalIconSize = 100;
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));
    App::Validator v(kLibcVersion);
    EXPECT_FALSE(v.aliasIsValid(f));
}

// ---------------------------------------------------------------------------
// ReadAliasPayload
// ---------------------------------------------------------------------------

TEST(AliasPayload, RoundTripsThroughFixture)
{
    AliasOpts o;
    o.targetAppID = 0x1122334455667788ULL;
    o.minTargetVersion = 42;
    o.origin = 1;
    MemFile f("2:/Apps/Walking/Walking.uapp", buildAlias(o));

    App::VariantAliasPayload p {};
    ASSERT_TRUE(App::ReadAliasPayload(f, p));
    EXPECT_EQ(p.payloadVersion, App::kAliasPayloadVersion);
    EXPECT_EQ(p.targetAppID, o.targetAppID);
    EXPECT_EQ(p.minTargetVersion, o.minTargetVersion);
    EXPECT_EQ(p.origin, o.origin);
    EXPECT_EQ(p.configSize, o.config.size());
}

// ---------------------------------------------------------------------------
// ResolveAlias
// ---------------------------------------------------------------------------

App::MainHeader aliasHeader(uint32_t flags = FLAG_APP_VARIANT_ALIAS)
{
    App::MainHeader h {};
    h.flags = flags;
    return h;
}

App::VariantAliasPayload aliasPayload(uint64_t target, uint32_t minVersion = 0)
{
    App::VariantAliasPayload p {};
    p.payloadVersion = App::kAliasPayloadVersion;
    p.targetAppID = target;
    p.minTargetVersion = minVersion;
    return p;
}

TEST(ResolveAlias, ResolvesAndSanitisesFlags)
{
    const App::MainHeader h = aliasHeader(
            FLAG_APP_VARIANT_ALIAS | FLAG_APP_AUTO_START | FLAG_APP_HAS_GLANCE_INTF);
    const App::VariantAliasPayload p = aliasPayload(7);
    const App::AliasTargetView targets[] = {
        {5, 1, 0, false},
        {7, 3, 0, false},
    };

    App::AliasResolution r = App::ResolveAlias(h, p, targets, 2);
    EXPECT_EQ(r.status, App::AliasResolution::Status::kOk);
    EXPECT_EQ(r.targetIndex, 1u);
    // Autostart, glance and the alias discriminator are all stripped from
    // the registered flags.
    EXPECT_EQ(r.flags, 0u);
    EXPECT_FALSE(r.typeAdjusted);
}

TEST(ResolveAlias, MissingTargetReported)
{
    const App::MainHeader h = aliasHeader();
    const App::VariantAliasPayload p = aliasPayload(99);
    const App::AliasTargetView targets[] = {{7, 3, 0, false}};

    App::AliasResolution r = App::ResolveAlias(h, p, targets, 1);
    EXPECT_EQ(r.status, App::AliasResolution::Status::kNoTarget);
}

TEST(ResolveAlias, EmptyTargetListReported)
{
    App::AliasResolution r = App::ResolveAlias(aliasHeader(), aliasPayload(7), nullptr, 0);
    EXPECT_EQ(r.status, App::AliasResolution::Status::kNoTarget);
}

TEST(ResolveAlias, VariantNeverATarget)
{
    // A variant carrying the wanted appID must not resolve as a target --
    // aliases chain to real binaries only.
    const App::AliasTargetView targets[] = {{7, 3, 0, true}};

    App::AliasResolution r = App::ResolveAlias(aliasHeader(), aliasPayload(7), targets, 1);
    EXPECT_EQ(r.status, App::AliasResolution::Status::kNoTarget);
}

TEST(ResolveAlias, StaleTargetVersionReported)
{
    const App::AliasTargetView targets[] = {{7, 3, 0, false}};

    App::AliasResolution r = App::ResolveAlias(aliasHeader(), aliasPayload(7, 4), targets, 1);
    EXPECT_EQ(r.status, App::AliasResolution::Status::kTargetTooOld);
}

TEST(ResolveAlias, ExactMinTargetVersionAccepted)
{
    const App::AliasTargetView targets[] = {{7, 4, 0, false}};

    App::AliasResolution r = App::ResolveAlias(aliasHeader(), aliasPayload(7, 4), targets, 1);
    EXPECT_EQ(r.status, App::AliasResolution::Status::kOk);
}

TEST(ResolveAlias, TypeBitsFollowTarget)
{
    // Alias claims utility (1); the target is a clockface-typed binary (3).
    const App::MainHeader h = aliasHeader(FLAG_APP_VARIANT_ALIAS | FLAG_APP_TYPE_UTILITY);
    const App::AliasTargetView targets[] = {{7, 1, FLAG_APP_TYPE_CLOCKFACE, false}};

    App::AliasResolution r = App::ResolveAlias(h, aliasPayload(7), targets, 1);
    EXPECT_EQ(r.status, App::AliasResolution::Status::kOk);
    EXPECT_TRUE(r.typeAdjusted);
    EXPECT_EQ(r.flags & FLAG_APP_TYPE_MASK,
            static_cast<uint32_t>(FLAG_APP_TYPE_CLOCKFACE));
}

TEST(ResolveAlias, OwnIdCollisionRejected)
{
    // Registry "id" uniqueness is a phone contract: an alias whose own
    // uappID is already registered (even by a later entry) must not resolve.
    App::MainHeader h = aliasHeader();
    h.uappID = 42;
    const App::AliasTargetView targets[] = {
        {7, 3, 0, false},
        {42, 1, 0, false},
    };

    App::AliasResolution r = App::ResolveAlias(h, aliasPayload(7), targets, 2);
    EXPECT_EQ(r.status, App::AliasResolution::Status::kIdCollision);
}

TEST(ResolveAlias, GlanceTargetRejected)
{
    // startGlances() runs every kGlance entry, so a glance variant would
    // double-load one binary with no user intent -- out of scope for v1.
    const App::AliasTargetView targets[] = {{7, 3, FLAG_APP_TYPE_GLANCE, false}};

    App::AliasResolution r = App::ResolveAlias(aliasHeader(), aliasPayload(7), targets, 1);
    EXPECT_EQ(r.status, App::AliasResolution::Status::kGlanceTarget);
}

// ---------------------------------------------------------------------------
// PickUappInDir
// ---------------------------------------------------------------------------

using HeaderMap = std::map<std::string, App::MainHeader>;

std::function<bool(const char*, App::MainHeader&)> readerFor(const HeaderMap &headers)
{
    return [&headers](const char *path, App::MainHeader &h) {
        auto it = headers.find(path);
        if (it == headers.end()) {
            return false;  // unreadable header
        }
        h = it->second;
        return true;
    };
}

constexpr char kDirPath[] = "2:/Apps/Test/";
constexpr size_t kMinSize = 900;

App::MainHeader realHeader()
{
    return App::MainHeader {};
}

TEST(PickUappInDir, SingleRealApp)
{
    MemDirectory dir({{"Hiking.uapp", false, 700000}});
    HeaderMap headers {{"2:/Apps/Test/Hiking.uapp", realHeader()}};
    char path[257] {};

    auto pick = App::PickUappInDir(dir, kDirPath, ".uapp", kMinSize,
            readerFor(headers), path, sizeof(path));
    EXPECT_EQ(pick, App::DirPick::kRealApp);
    EXPECT_STREQ(path, "2:/Apps/Test/Hiking.uapp");
}

TEST(PickUappInDir, SingleAlias)
{
    MemDirectory dir({{"Walking.uapp", false, 4600}});
    HeaderMap headers {{"2:/Apps/Test/Walking.uapp", aliasHeader()}};
    char path[257] {};

    auto pick = App::PickUappInDir(dir, kDirPath, ".uapp", kMinSize,
            readerFor(headers), path, sizeof(path));
    EXPECT_EQ(pick, App::DirPick::kAlias);
    EXPECT_STREQ(path, "2:/Apps/Test/Walking.uapp");
}

TEST(PickUappInDir, RealAppBeatsEarlierAlias)
{
    MemDirectory dir({
        {"AAlias.uapp", false, 4600},
        {"Hiking.uapp", false, 700000},
    });
    HeaderMap headers {
        {"2:/Apps/Test/AAlias.uapp", aliasHeader()},
        {"2:/Apps/Test/Hiking.uapp", realHeader()},
    };
    char path[257] {};

    auto pick = App::PickUappInDir(dir, kDirPath, ".uapp", kMinSize,
            readerFor(headers), path, sizeof(path));
    EXPECT_EQ(pick, App::DirPick::kRealApp);
    EXPECT_STREQ(path, "2:/Apps/Test/Hiking.uapp");
}

TEST(PickUappInDir, FirstRealAppStopsTheWalk)
{
    MemDirectory dir({
        {"First.uapp", false, 700000},
        {"Second.uapp", false, 700000},
    });
    HeaderMap headers {
        {"2:/Apps/Test/First.uapp", realHeader()},
        {"2:/Apps/Test/Second.uapp", realHeader()},
    };
    char path[257] {};

    auto pick = App::PickUappInDir(dir, kDirPath, ".uapp", kMinSize,
            readerFor(headers), path, sizeof(path));
    EXPECT_EQ(pick, App::DirPick::kRealApp);
    EXPECT_STREQ(path, "2:/Apps/Test/First.uapp");
}

TEST(PickUappInDir, FirstAliasWins)
{
    MemDirectory dir({
        {"One.uapp", false, 4600},
        {"Two.uapp", false, 4600},
    });
    HeaderMap headers {
        {"2:/Apps/Test/One.uapp", aliasHeader()},
        {"2:/Apps/Test/Two.uapp", aliasHeader()},
    };
    char path[257] {};

    auto pick = App::PickUappInDir(dir, kDirPath, ".uapp", kMinSize,
            readerFor(headers), path, sizeof(path));
    EXPECT_EQ(pick, App::DirPick::kAlias);
    EXPECT_STREQ(path, "2:/Apps/Test/One.uapp");
}

TEST(PickUappInDir, IgnoresNonCandidates)
{
    MemDirectory dir({
        {"readme.txt", false, 5000},     // wrong extension
        {"Tiny.uapp", false, 100},       // at/below the icon-size floor
        {"Activity", true, 0},           // directory
        {"Hiking.uapp", false, 700000},
    });
    HeaderMap headers {{"2:/Apps/Test/Hiking.uapp", realHeader()}};
    char path[257] {};

    auto pick = App::PickUappInDir(dir, kDirPath, ".uapp", kMinSize,
            readerFor(headers), path, sizeof(path));
    EXPECT_EQ(pick, App::DirPick::kRealApp);
    EXPECT_STREQ(path, "2:/Apps/Test/Hiking.uapp");
}

TEST(PickUappInDir, UnreadableHeaderRoutesToAddApp)
{
    // A candidate whose header cannot be read must be handed to addApp() so
    // the failure is validated and logged there (pre-alias behaviour).
    MemDirectory dir({{"Broken.uapp", false, 5000}});
    HeaderMap headers {};  // reader fails for every path
    char path[257] {};

    auto pick = App::PickUappInDir(dir, kDirPath, ".uapp", kMinSize,
            readerFor(headers), path, sizeof(path));
    EXPECT_EQ(pick, App::DirPick::kRealApp);
    EXPECT_STREQ(path, "2:/Apps/Test/Broken.uapp");
}

TEST(PickUappInDir, EmptyDirPicksNothing)
{
    MemDirectory dir({});
    HeaderMap headers {};
    char path[257] {};

    auto pick = App::PickUappInDir(dir, kDirPath, ".uapp", kMinSize,
            readerFor(headers), path, sizeof(path));
    EXPECT_EQ(pick, App::DirPick::kNone);
}

} // namespace
