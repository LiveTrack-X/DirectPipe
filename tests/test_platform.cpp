// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Platform/AutoStart.h"
#include "Platform/ProcessPriority.h"
#include "Platform/MultiInstanceLock.h"

using namespace directpipe;

class PlatformTest : public ::testing::Test {
protected:
    void TearDown() override {
        Platform::setAutoStartEnabled(false);
    }
};

TEST_F(PlatformTest, AutoStartToggle) {
    if (!Platform::isAutoStartSupported()) {
        GTEST_SKIP() << "AutoStart not supported on this platform";
    }
    Platform::setAutoStartEnabled(true);
    EXPECT_TRUE(Platform::isAutoStartEnabled());
}

TEST_F(PlatformTest, AutoStartDisable) {
    if (!Platform::isAutoStartSupported()) {
        GTEST_SKIP() << "AutoStart not supported on this platform";
    }
    Platform::setAutoStartEnabled(true);
    Platform::setAutoStartEnabled(false);
    EXPECT_FALSE(Platform::isAutoStartEnabled());
}

TEST_F(PlatformTest, AutoStartSupported) {
    bool supported = Platform::isAutoStartSupported();
#if JUCE_WINDOWS || JUCE_MAC
    EXPECT_TRUE(supported);
#elif JUCE_LINUX
    (void)supported;
#endif
    SUCCEED();
}

TEST_F(PlatformTest, ProcessPriorityHigh) {
    Platform::setHighPriority();
    SUCCEED();
}

TEST_F(PlatformTest, ProcessPriorityRestore) {
    Platform::setHighPriority();
    Platform::restoreNormalPriority();
    SUCCEED();
}

TEST_F(PlatformTest, MultiInstanceAcquire) {
    int result = Platform::acquireExternalControlPriority(false);
    EXPECT_NE(result, 0);
}

TEST_F(PlatformTest, MultiInstanceAlreadyHeld) {
    int first = Platform::acquireExternalControlPriority(false);
    if (first != 1) {
        GTEST_SKIP() << "Could not acquire lock for test";
    }
    int second = Platform::acquireExternalControlPriority(false);
    (void)second;
    SUCCEED();
}
