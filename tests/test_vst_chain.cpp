// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Audio/VSTChain.h"

using namespace directpipe;

class VSTChainTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure MessageManager exists — the first call creates it on this thread,
        // making this thread the "message thread" for jassert checks.
        juce::MessageManager::getInstance();

        chain_ = std::make_unique<VSTChain>();
        chain_->prepareToPlay(48000.0, 512);
    }

    void TearDown() override {
        chain_.reset();
    }

    // Helper: add a built-in processor and assert success
    void addBuiltin(PluginSlot::Type type) {
        auto r = chain_->addBuiltinProcessor(type);
        ASSERT_TRUE(r.success) << "addBuiltinProcessor failed: " << r.message.toStdString();
    }

    std::unique_ptr<VSTChain> chain_;
};

// Test 1: VSTChain starts with zero plugins
TEST_F(VSTChainTest, InitialEmpty) {
    EXPECT_EQ(chain_->getPluginCount(), 0);
}

// Test 2: addBuiltinProcessor adds a BuiltinFilter and count becomes 1
TEST_F(VSTChainTest, AddBuiltinFilter) {
    auto result = chain_->addBuiltinProcessor(PluginSlot::Type::BuiltinFilter);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(chain_->getPluginCount(), 1);

    auto* slot = chain_->getPluginSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->type, PluginSlot::Type::BuiltinFilter);
}

// Test 3: Add 2 built-in processors, remove first, count goes from 2 to 1
TEST_F(VSTChainTest, AddRemovePlugin) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    addBuiltin(PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(chain_->getPluginCount(), 2);

    bool removed = chain_->removePlugin(0);
    EXPECT_TRUE(removed);
    EXPECT_EQ(chain_->getPluginCount(), 1);

    // The remaining plugin should be the NR (was at index 1, now at index 0)
    auto* slot = chain_->getPluginSlot(0);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->type, PluginSlot::Type::BuiltinNoiseRemoval);
}

// Test 4: movePlugin swaps order — Filter+NR → NR+Filter after move(0,1)
TEST_F(VSTChainTest, MovePlugin) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    addBuiltin(PluginSlot::Type::BuiltinNoiseRemoval);

    bool moved = chain_->movePlugin(PluginIndex{0}, PluginIndex{1});
    EXPECT_TRUE(moved);

    auto* slot0 = chain_->getPluginSlot(0);
    auto* slot1 = chain_->getPluginSlot(1);
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    EXPECT_EQ(slot0->type, PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(slot1->type, PluginSlot::Type::BuiltinFilter);
}

// Test 5: Bypass toggle — set true, verify, set false, verify
TEST_F(VSTChainTest, BypassToggle) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);

    EXPECT_FALSE(chain_->isPluginBypassed(0));

    chain_->setPluginBypassed(0, true);
    EXPECT_TRUE(chain_->isPluginBypassed(0));

    chain_->setPluginBypassed(0, false);
    EXPECT_FALSE(chain_->isPluginBypassed(0));
}

// Test 6: Each built-in type correctly reports its type via getPluginSlot
TEST_F(VSTChainTest, PluginSlotType) {
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    addBuiltin(PluginSlot::Type::BuiltinNoiseRemoval);
    addBuiltin(PluginSlot::Type::BuiltinAutoGain);

    EXPECT_EQ(chain_->getPluginCount(), 3);

    auto* slot0 = chain_->getPluginSlot(0);
    auto* slot1 = chain_->getPluginSlot(1);
    auto* slot2 = chain_->getPluginSlot(2);
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    ASSERT_NE(slot2, nullptr);

    EXPECT_EQ(slot0->type, PluginSlot::Type::BuiltinFilter);
    EXPECT_EQ(slot1->type, PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(slot2->type, PluginSlot::Type::BuiltinAutoGain);
}

// Test 7: addAutoProcessors adds all 3 in Filter+NR+AGC order
TEST_F(VSTChainTest, AutoProcessors) {
    auto result = chain_->addAutoProcessors();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(chain_->getPluginCount(), 3);

    auto* slot0 = chain_->getPluginSlot(0);
    auto* slot1 = chain_->getPluginSlot(1);
    auto* slot2 = chain_->getPluginSlot(2);
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    ASSERT_NE(slot2, nullptr);

    EXPECT_EQ(slot0->type, PluginSlot::Type::BuiltinFilter);
    EXPECT_EQ(slot1->type, PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(slot2->type, PluginSlot::Type::BuiltinAutoGain);
}

// Test 8: addAutoProcessors skips existing Filter, only adds NR+AGC
TEST_F(VSTChainTest, AutoProcessorsNoDuplicate) {
    // Manually add Filter first
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    EXPECT_EQ(chain_->getPluginCount(), 1);

    // addAutoProcessors should add NR+AGC but not a duplicate Filter
    auto result = chain_->addAutoProcessors();
    EXPECT_TRUE(result.success);
    EXPECT_EQ(chain_->getPluginCount(), 3);

    // Verify order: addAutoProcessors inserts at front starting from insertPos=0.
    //   hasFilter=true -> skip, insertPos stays 0
    //   hasNR=false -> insert NR at 0 -> insertPos becomes 1
    //   hasAGC=false -> insert AGC at 1
    //   Result: [NR, AGC, Filter]
    auto* slot0 = chain_->getPluginSlot(0);
    auto* slot1 = chain_->getPluginSlot(1);
    auto* slot2 = chain_->getPluginSlot(2);
    ASSERT_NE(slot0, nullptr);
    ASSERT_NE(slot1, nullptr);
    ASSERT_NE(slot2, nullptr);

    EXPECT_EQ(slot0->type, PluginSlot::Type::BuiltinNoiseRemoval);
    EXPECT_EQ(slot1->type, PluginSlot::Type::BuiltinAutoGain);
    EXPECT_EQ(slot2->type, PluginSlot::Type::BuiltinFilter);
}

// Test 9: removePlugin with out-of-range indices returns false gracefully
TEST_F(VSTChainTest, RemoveOutOfRange) {
    // Empty chain — any index is out of range
    EXPECT_FALSE(chain_->removePlugin(-1));
    EXPECT_FALSE(chain_->removePlugin(0));
    EXPECT_FALSE(chain_->removePlugin(999));

    // Add one plugin — only index 0 is valid
    addBuiltin(PluginSlot::Type::BuiltinFilter);
    EXPECT_FALSE(chain_->removePlugin(-1));
    EXPECT_FALSE(chain_->removePlugin(1));
    EXPECT_FALSE(chain_->removePlugin(999));

    // Index 0 should succeed
    EXPECT_TRUE(chain_->removePlugin(0));
    EXPECT_EQ(chain_->getPluginCount(), 0);
}
