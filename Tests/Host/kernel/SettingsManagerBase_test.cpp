/**
 ******************************************************************************
 * @file    SettingsManagerBase_test.cpp
 * @brief   Host tests for Settings::ManagerBase::updateSettings(): the cached
 *          settings change only once the file is written, over a fake
 *          volume that fails on demand.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include "Settings/SettingsManagerBase.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>

namespace {

struct TestSettings {
    int  level = 1;
    bool flag  = false;
};

constexpr const char *kPath = "2:/test.json";
const std::string kTmp = std::string(kPath) + ".tmp";
const std::string kBak = std::string(kPath) + ".bak";

std::string text(int level, bool flag)
{
    return std::to_string(level) + "," + (flag ? "1" : "0");
}

class TestManager : public Settings::ManagerBase<TestSettings, 64> {
public:
    TestManager() : ManagerBase(kPath) {}

    int changes = 0;

    bool setFlag(bool on)
    {
        return updateSettings([this, on](TestSettings &s) {
            ++changes;
            s.flag = on;
        });
    }

protected:
    bool parse(const char *buff, size_t length, TestSettings &settings) override
    {
        const std::string in(buff, length);
        int level = 0;
        int flag  = 0;
        if (std::sscanf(in.c_str(), "%d,%d", &level, &flag) != 2) {
            return false;
        }
        settings.level = level;
        settings.flag  = flag != 0;
        return true;
    }

    size_t create(const TestSettings &settings, char *buff, size_t buffSize) override
    {
        const std::string out = text(settings.level, settings.flag);
        if (out.size() >= buffSize) {
            return 0;
        }
        std::memcpy(buff, out.data(), out.size());
        return out.size();
    }
};

class SettingsUpdate : public ::testing::Test {
protected:
    void SetUp() override { FS::FakeVolume::Reset(); }
    void TearDown() override { FS::FakeVolume::Reset(); }

    static std::map<std::string, std::string> &files() { return FS::FakeVolume::files; }
};

} // namespace

TEST_F(SettingsUpdate, SavesOnlyTheChangedSetting)
{
    files()[kPath] = text(7, false);
    TestManager m;
    ASSERT_TRUE(m.load());

    EXPECT_TRUE(m.setFlag(true));

    EXPECT_EQ(files()[kPath], text(7, true));
    EXPECT_EQ(files()[kBak], text(7, false));
    EXPECT_EQ(m.getSettings().level, 7);
    EXPECT_TRUE(m.getSettings().flag);
}

// The case behind it: no file and no backup, so load() fails and cannot
// put the cache back after a failed write.
TEST_F(SettingsUpdate, FirstSaveFailingKeepsTheDefaults)
{
    TestManager m;
    ASSERT_FALSE(m.load());
    FS::FakeVolume::failWriteOpen = kTmp;

    EXPECT_FALSE(m.setFlag(true));

    EXPECT_FALSE(m.getSettings().flag);
    EXPECT_TRUE(files().empty());
}

TEST_F(SettingsUpdate, FirstSaveFailingAtTheRenameKeepsTheDefaults)
{
    TestManager m;
    FS::FakeVolume::failRenameTo = kPath;

    EXPECT_FALSE(m.setFlag(true));

    EXPECT_FALSE(m.getSettings().flag);
    EXPECT_EQ(files().count(kPath), 0u);
}

TEST_F(SettingsUpdate, FailedWriteKeepsTheFileAndTheCache)
{
    files()[kPath] = text(7, false);
    TestManager m;
    ASSERT_TRUE(m.load());
    FS::FakeVolume::failWriteOpen = kTmp;

    EXPECT_FALSE(m.setFlag(true));

    EXPECT_FALSE(m.getSettings().flag);
    EXPECT_EQ(files()[kPath], text(7, false));
}

// The primary is already rotated to .bak when the promote fails. The cache
// keeps the old value, which is what load() then recovers.
TEST_F(SettingsUpdate, FailedPromoteMatchesWhatLoadRecovers)
{
    files()[kPath] = text(7, false);
    TestManager m;
    ASSERT_TRUE(m.load());
    FS::FakeVolume::failRenameTo = kPath;

    EXPECT_FALSE(m.setFlag(true));
    EXPECT_FALSE(m.getSettings().flag);

    FS::FakeVolume::failRenameTo.clear();
    ASSERT_TRUE(m.load());
    EXPECT_FALSE(m.getSettings().flag);
    EXPECT_EQ(m.getSettings().level, 7);
}

// No lock (the stand-in for a lock timeout): nothing is changed or written.
// getSettings() + saveSettings() would have written the defaults here.
TEST_F(SettingsUpdate, NoLockChangesNothing)
{
    files()[kPath] = text(7, false);
    std::unique_ptr<TestManager> m;
    {
        OS::MutexInitFailure noMutex;
        m = std::make_unique<TestManager>();
    }

    EXPECT_FALSE(m->setFlag(true));

    EXPECT_EQ(m->changes, 0);
    EXPECT_EQ(files()[kPath], text(7, false));
    EXPECT_EQ(files().count(kBak), 0u);
}
