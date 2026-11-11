/**
 * Host unit tests for App::TypeFromFlags -- the whole of the answer to "what is
 * this app".
 *
 * The type used to be derived in Application::getType(), which folded the
 * glance-interface flag into the Glance branch and tested that branch before
 * Clockface. A Clockface carrying the flag therefore reported kGlance. Activity
 * and Utility resolved earlier and were unaffected, which is the only reason it
 * stayed invisible.
 *
 * So the property worth pinning is not the four happy cases but the negative
 * one: no bit outside FLAG_APP_TYPE_MASK may change the answer. That is swept
 * exhaustively below rather than sampled, because the defect was precisely one
 * unrelated bit reaching into the decision.
 */

#include "App/AppHeaders.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

TEST(AppType, MaskSelectsTheBaseType)
{
    EXPECT_EQ(App::TypeFromFlags(FLAG_APP_TYPE_ACTIVITY),  App::Type::kActivity);
    EXPECT_EQ(App::TypeFromFlags(FLAG_APP_TYPE_UTILITY),   App::Type::kUtility);
    EXPECT_EQ(App::TypeFromFlags(FLAG_APP_TYPE_GLANCE),    App::Type::kGlance);
    EXPECT_EQ(App::TypeFromFlags(FLAG_APP_TYPE_CLOCKFACE), App::Type::kClockface);
}

// The regression. Every .uapp built before SDK bb82a621 carries the glance
// bit whatever its type, so this combination is the common case in the field,
// not a corner one.
TEST(AppType, GlanceInterfaceFlagDoesNotChangeTheType)
{
    EXPECT_EQ(App::TypeFromFlags(FLAG_APP_TYPE_ACTIVITY  | FLAG_APP_HAS_GLANCE_INTF),
              App::Type::kActivity);
    EXPECT_EQ(App::TypeFromFlags(FLAG_APP_TYPE_UTILITY   | FLAG_APP_HAS_GLANCE_INTF),
              App::Type::kUtility);
    EXPECT_EQ(App::TypeFromFlags(FLAG_APP_TYPE_GLANCE    | FLAG_APP_HAS_GLANCE_INTF),
              App::Type::kGlance);
    EXPECT_EQ(App::TypeFromFlags(FLAG_APP_TYPE_CLOCKFACE | FLAG_APP_HAS_GLANCE_INTF),
              App::Type::kClockface);
}

// Sweeps the whole byte the validator can let through (and the bits above it
// that only an unvalidated header would carry), so any future flag that reaches
// into the type decision fails here rather than in the field.
TEST(AppType, NoBitOutsideTheMaskTakesPart)
{
    for (uint32_t high = 0; high < 256; ++high) {
        const uint32_t other = high << 2;   // every bit except the type mask
        for (uint32_t type = 0; type <= FLAG_APP_TYPE_MASK; ++type) {
            EXPECT_EQ(App::TypeFromFlags(type | other), App::TypeFromFlags(type))
                    << "flags 0x" << std::hex << (type | other);
        }
    }
}

// All four mask values are assigned, so the fall-through arm is unreachable.
TEST(AppType, UnknownIsNeverReturned)
{
    for (uint32_t flags = 0; flags < 1024; ++flags) {
        EXPECT_NE(App::TypeFromFlags(flags), App::Type::kUnknown)
                << "flags 0x" << std::hex << flags;
    }
}

} // namespace
