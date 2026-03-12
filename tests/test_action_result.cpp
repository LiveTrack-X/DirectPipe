// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file test_action_result.cpp
 * @brief Unit tests for ActionResult lightweight result type
 */

#include <gtest/gtest.h>
#include "ActionResult.h"

using namespace directpipe;

// ─── Construction ──────────────────────────────────────────────────────────

TEST(ActionResultTest, OkIsSuccess) {
    auto r = ActionResult::ok();
    EXPECT_TRUE(r.success);
    EXPECT_TRUE(r.message.isEmpty());
}

TEST(ActionResultTest, OkWithMessage) {
    auto r = ActionResult::ok("Buffer adjusted to 512");
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.message, juce::String("Buffer adjusted to 512"));
}

TEST(ActionResultTest, FailIsNotSuccess) {
    auto r = ActionResult::fail("Device not found");
    EXPECT_FALSE(r.success);
    EXPECT_EQ(r.message, juce::String("Device not found"));
}

TEST(ActionResultTest, FailPreservesMessage) {
    juce::String msg = "Failed to set sample rate 96000: not supported";
    auto r = ActionResult::fail(msg);
    EXPECT_EQ(r.message, msg);
}

// ─── operator bool() ───────────────────────────────────────────────────────

TEST(ActionResultTest, OkIsTrue) {
    auto r = ActionResult::ok();
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ActionResultTest, FailIsFalse) {
    auto r = ActionResult::fail("error");
    EXPECT_FALSE(static_cast<bool>(r));
}

TEST(ActionResultTest, OkWithMessageIsTrue) {
    auto r = ActionResult::ok("info message");
    EXPECT_TRUE(static_cast<bool>(r));
}

TEST(ActionResultTest, IfConditionOk) {
    auto r = ActionResult::ok();
    bool entered = false;
    if (r) entered = true;
    EXPECT_TRUE(entered);
}

TEST(ActionResultTest, IfConditionFail) {
    auto r = ActionResult::fail("err");
    bool entered = false;
    if (!r) entered = true;
    EXPECT_TRUE(entered);
}

// ─── Edge cases ────────────────────────────────────────────────────────────

TEST(ActionResultTest, FailWithEmptyMessage) {
    auto r = ActionResult::fail("");
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.message.isEmpty());
}

TEST(ActionResultTest, OkDefaultMessageEmpty) {
    auto r = ActionResult::ok();
    EXPECT_EQ(r.message.length(), 0);
}

TEST(ActionResultTest, FailMessageWithUnicode) {
    auto r = ActionResult::fail(juce::String::fromUTF8("장치를 찾을 수 없음"));
    EXPECT_FALSE(r.success);
    EXPECT_TRUE(r.message.isNotEmpty());
}
