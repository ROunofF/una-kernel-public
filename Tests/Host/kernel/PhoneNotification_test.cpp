// Host unit tests for the PhoneNotification identity and content semantics.
//
// The equality operator is the dedup key for the GUI notification list: it
// must be stable across re-fetches of the same notification, or every update
// appends a fresh list entry and the list grows without bound.

#include <gtest/gtest.h>

#include <ctime>

#include "Components/Shared/PhoneNotification.hpp"

namespace {

PhoneNotification Make(uint32_t rxms, PhoneNotification::Platform platform,
        uint32_t uid, const char *title, const char *msg,
        PhoneNotification::Category category =
                PhoneNotification::Category::MESSAGE)
{
    std::tm ts {};
    return PhoneNotification(rxms, platform, uid, category, ts, title, "sub",
            msg, "app.id", "OK", "Clear");
}

} // namespace

TEST(PhoneNotification, EqualityIgnoresReceiveTimeAndContent)
{
    // The same notification re-fetched later (new rxTimeMs, updated body)
    // must compare equal so consumers replace it instead of appending.
    PhoneNotification a = Make(1000, PhoneNotification::Platform::ANDROID,
            42, "Chat", "first message");
    PhoneNotification b = Make(2000, PhoneNotification::Platform::ANDROID,
            42, "Chat", "second message");

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(PhoneNotification, EqualityRequiresUidAndPlatform)
{
    PhoneNotification a = Make(1000, PhoneNotification::Platform::ANDROID,
            42, "Chat", "hello");

    PhoneNotification otherUid = Make(1000,
            PhoneNotification::Platform::ANDROID, 43, "Chat", "hello");
    EXPECT_FALSE(a == otherUid);

    PhoneNotification otherPlatform = Make(1000,
            PhoneNotification::Platform::IOS, 42, "Chat", "hello");
    EXPECT_FALSE(a == otherPlatform);
}

TEST(PhoneNotification, HasSameContentComparesVisibleText)
{
    PhoneNotification a = Make(1000, PhoneNotification::Platform::ANDROID,
            42, "Chat", "hello");

    PhoneNotification rePost = Make(2000, PhoneNotification::Platform::ANDROID,
            42, "Chat", "hello");
    EXPECT_TRUE(a.hasSameContent(rePost));

    PhoneNotification newMessage = Make(2000,
            PhoneNotification::Platform::ANDROID, 42, "Chat", "a new reply");
    EXPECT_FALSE(a.hasSameContent(newMessage));

    PhoneNotification newTitle = Make(2000,
            PhoneNotification::Platform::ANDROID, 42, "Chat (2)", "hello");
    EXPECT_FALSE(a.hasSameContent(newTitle));
}

TEST(PhoneNotification, HasSameContentDetectsCategoryChange)
{
    // A call-state transition (incoming -> missed) often changes nothing but
    // the category — the text is the caller's name either way. The popup
    // layout is driven by the category, so this must count as a real update.
    PhoneNotification incoming = Make(1000,
            PhoneNotification::Platform::ANDROID, 42, "Lucy", "",
            PhoneNotification::Category::INCOMING_CALL);
    PhoneNotification missed = Make(2000,
            PhoneNotification::Platform::ANDROID, 42, "Lucy", "",
            PhoneNotification::Category::MISSED_CALL);

    EXPECT_TRUE(incoming == missed);  // same notification (identity)...
    EXPECT_FALSE(incoming.hasSameContent(missed));  // ...but a real update
}
