// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025-2026 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>

#include "Platform/EndpointChangeWatcher.h"

namespace directpipe {
namespace {

#if JUCE_WINDOWS
constexpr auto kSelectedEndpointId =
    "{0.0.1.00000000}.{c31f6564-d57a-419a-8fd0-94c5fdfd5000}";
constexpr auto kSameNameOtherEndpointId =
    "{0.0.1.00000000}.{d2d84c41-d8c5-4a10-8c28-8a9ee5a74de8}";

TEST(EndpointChangeWatcherTest, QueuesOnlyTheExactSelectedEndpointUntilDrain)
{
    EndpointChangeWatcher watcher;
    int callbackCount = 0;
    juce::String callbackDevice;
    juce::String callbackReason;

    watcher.setInputDeviceName("Analog 1/2 (E2x2 OTG)");
    watcher.setCallback([&](const juce::String& device, const juce::String& reason) {
        ++callbackCount;
        callbackDevice = device;
        callbackReason = reason;
    });
    EndpointChangeWatcherTestAccess::setResolvedEndpointId(watcher, kSelectedEndpointId);

    EXPECT_FALSE(EndpointChangeWatcherTestAccess::signalEndpointEvent(
        watcher,
        kSameNameOtherEndpointId,
        EndpointChangeWatcherTestAccess::Event::propertyChanged));
    EXPECT_EQ(EndpointChangeWatcherTestAccess::drainPendingEvents(watcher), 0);
    EXPECT_EQ(callbackCount, 0);

    EXPECT_TRUE(EndpointChangeWatcherTestAccess::signalEndpointEvent(
        watcher,
        kSelectedEndpointId,
        EndpointChangeWatcherTestAccess::Event::propertyChanged));
    EXPECT_EQ(callbackCount, 0) << "notification callbacks must only enqueue";

    EXPECT_EQ(EndpointChangeWatcherTestAccess::drainPendingEvents(watcher), 1);
    EXPECT_EQ(callbackCount, 1);
    EXPECT_EQ(callbackDevice, "Analog 1/2 (E2x2 OTG)");
    EXPECT_EQ(callbackReason, "capture endpoint property changed");
    EXPECT_EQ(EndpointChangeWatcherTestAccess::drainPendingEvents(watcher), 0);
}

TEST(EndpointChangeWatcherTest, CoalescesRepeatedSignalsAndIgnoresDefaultRoleChanges)
{
    EndpointChangeWatcher watcher;
    int callbackCount = 0;

    watcher.setInputDeviceName("Analog 1/2 (E2x2 OTG)");
    watcher.setCallback([&](const juce::String&, const juce::String&) {
        ++callbackCount;
    });
    EndpointChangeWatcherTestAccess::setResolvedEndpointId(watcher, kSelectedEndpointId);

    EXPECT_FALSE(EndpointChangeWatcherTestAccess::signalEndpointEvent(
        watcher,
        kSelectedEndpointId,
        EndpointChangeWatcherTestAccess::Event::defaultDeviceChanged));

    EXPECT_TRUE(EndpointChangeWatcherTestAccess::signalEndpointEvent(
        watcher,
        kSelectedEndpointId,
        EndpointChangeWatcherTestAccess::Event::deviceStateChanged,
        0x00000008));
    EXPECT_TRUE(EndpointChangeWatcherTestAccess::signalEndpointEvent(
        watcher,
        kSelectedEndpointId,
        EndpointChangeWatcherTestAccess::Event::deviceStateChanged,
        0x00000001));

    EXPECT_EQ(callbackCount, 0);
    EXPECT_EQ(EndpointChangeWatcherTestAccess::drainPendingEvents(watcher), 1);
    EXPECT_EQ(callbackCount, 1);
}
#else
TEST(EndpointChangeWatcherTest, IsWindowsOnly)
{
    GTEST_SKIP() << "Core Audio endpoint notifications are Windows-only";
}
#endif

} // namespace
} // namespace directpipe
