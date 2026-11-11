/**
 ******************************************************************************
 * @file    ErrorArbiter_test.cpp
 * @brief   Host tests for the error-screen arbitration + acknowledge model.
 ******************************************************************************
 */

#include <gtest/gtest.h>

#include "ErrorArbiter.hpp"

TEST(ErrorArbiter, EmptyShowsNothing)
{
    ErrorArbiter a;
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);
}

TEST(ErrorArbiter, ShowsHighestPriorityActive)
{
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_AMBIENT);
    a.setActive(SystemError::USB_PROTECTION);      // higher priority
    a.setActive(SystemError::CABLE_CONNECTION);
    EXPECT_EQ(a.currentDisplay(), SystemError::USB_PROTECTION);
}

TEST(ErrorArbiter, ClearingTopRevealsNextActive)
{
    ErrorArbiter a;
    a.setActive(SystemError::USB_PROTECTION);
    a.setActive(SystemError::TEMPERATURE_AMBIENT);
    ASSERT_EQ(a.currentDisplay(), SystemError::USB_PROTECTION);

    a.clearActive(SystemError::USB_PROTECTION);
    EXPECT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_AMBIENT);
}

TEST(ErrorArbiter, AcknowledgeHidesCurrentAndLowerButNotHigher)
{
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_CABLE);
    ASSERT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_CABLE);

    a.acknowledge();                               // suppress cable and below
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);

    // A lower-priority condition stays suppressed.
    a.setActive(SystemError::TEMPERATURE_AMBIENT);
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);

    // A strictly higher one (escalation) re-shows.
    a.setActive(SystemError::TEMPERATURE_CRITICAL);
    EXPECT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_CRITICAL);
}

TEST(ErrorArbiter, DeEscalationDoesNotRenagAfterAcknowledge)
{
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_CABLE);   // dismissable, prio 4
    a.setActive(SystemError::TEMPERATURE_AMBIENT); // lower, prio 3
    ASSERT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_CABLE);

    a.acknowledge();                               // ack at the cable level
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);

    // The still-active lower condition stays suppressed (no re-nag).
    a.clearActive(SystemError::TEMPERATURE_CABLE);
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);
}

TEST(ErrorArbiter, FullRecoveryResetsAcknowledge)
{
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_CABLE);
    a.acknowledge();
    ASSERT_EQ(a.currentDisplay(), SystemError::NONE);

    a.clearActive(SystemError::TEMPERATURE_CABLE); // no conditions -> reset
    // A fresh episode of the same error shows again.
    a.setActive(SystemError::TEMPERATURE_CABLE);
    EXPECT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_CABLE);
}

TEST(ErrorArbiter, ShutdownIsNotDismissable)
{
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_SHUTDOWN);
    ASSERT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_SHUTDOWN);

    a.acknowledge();                               // ignored
    EXPECT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_SHUTDOWN);
}

TEST(ErrorArbiter, CriticalIsNotDismissable)
{
    // die>55 + USB "disconnect cable": must stay until the condition resolves
    // (cable pulled -> shutdown, or cooled -> de-escalates), not on a dismiss.
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_CRITICAL);
    ASSERT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_CRITICAL);

    a.acknowledge();                               // ignored
    EXPECT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_CRITICAL);

    // It clears when its condition resolves, not by the user.
    a.clearActive(SystemError::TEMPERATURE_CRITICAL);
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);
}

TEST(ErrorArbiter, AcknowledgeWithNothingShownIsNoOp)
{
    ErrorArbiter a;
    a.acknowledge();
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);

    // Ack level untouched, so a later error still shows.
    a.setActive(SystemError::TEMPERATURE_AMBIENT);
    EXPECT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_AMBIENT);
}

TEST(ErrorArbiter, ResetAcknowledgeReShowsStillActiveError)
{
    // The user dismisses "ambient too high", then re-plugs the cable to retry
    // charging. The condition still blocks it, so it must be shown again.
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_AMBIENT);
    a.acknowledge();
    ASSERT_EQ(a.currentDisplay(), SystemError::NONE);

    a.resetAcknowledge();
    EXPECT_EQ(a.currentDisplay(), SystemError::TEMPERATURE_AMBIENT);

    // Dismissable again after the retry.
    a.acknowledge();
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);
}

TEST(ErrorArbiter, ResetAcknowledgeShowsNothingWhenRecovered)
{
    // A retry after the condition resolved must stay silent.
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_AMBIENT);
    a.acknowledge();
    a.clearActive(SystemError::TEMPERATURE_AMBIENT);

    a.resetAcknowledge();
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);
}

TEST(ErrorArbiter, ConditionIgnoresAcknowledge)
{
    // The phone is told about the hazard, so hiding the message on the watch
    // must not look like a recovery.
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_AMBIENT);
    EXPECT_EQ(a.currentCondition(), SystemError::TEMPERATURE_AMBIENT);

    a.acknowledge();
    EXPECT_EQ(a.currentDisplay(), SystemError::NONE);          // screen hidden
    EXPECT_EQ(a.currentCondition(), SystemError::TEMPERATURE_AMBIENT);   // hazard stands

    a.clearActive(SystemError::TEMPERATURE_AMBIENT);
    EXPECT_EQ(a.currentCondition(), SystemError::NONE);        // real recovery
}

TEST(ErrorArbiter, ConditionFollowsThePriorityOrder)
{
    ErrorArbiter a;
    a.setActive(SystemError::TEMPERATURE_AMBIENT);
    a.setActive(SystemError::USB_PROTECTION);
    EXPECT_EQ(a.currentCondition(), SystemError::USB_PROTECTION);

    // De-escalation is visible even while the top one was acknowledged.
    a.acknowledge();
    a.clearActive(SystemError::USB_PROTECTION);
    EXPECT_EQ(a.currentCondition(), SystemError::TEMPERATURE_AMBIENT);
}

TEST(ErrorArbiter, ConditionIsUnknownWhenNothingActive)
{
    ErrorArbiter a;
    EXPECT_EQ(a.currentCondition(), SystemError::NONE);
}
