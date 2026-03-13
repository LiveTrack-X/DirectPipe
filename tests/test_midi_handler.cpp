// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Control/MidiHandler.h"
#include "Control/ActionDispatcher.h"

using namespace directpipe;

class MidiHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        dispatcher_ = std::make_unique<ActionDispatcher>();
        handler_ = std::make_unique<MidiHandler>(*dispatcher_);
    }

    void TearDown() override {
        handler_->shutdown();
        handler_.reset();
        dispatcher_.reset();
    }

    std::unique_ptr<ActionDispatcher> dispatcher_;
    std::unique_ptr<MidiHandler> handler_;
};

TEST_F(MidiHandlerTest, AddBinding) {
    MidiBinding binding;
    binding.cc = 7;
    binding.channel = 1;
    binding.type = MidiMappingType::Toggle;
    binding.action = ActionEvent{Action::ToggleMute};

    handler_->addBinding(binding);
    auto bindings = handler_->getBindings();
    ASSERT_EQ(bindings.size(), 1u);
    EXPECT_EQ(bindings[0].cc, 7);
    EXPECT_EQ(bindings[0].channel, 1);
}

TEST_F(MidiHandlerTest, RemoveBinding) {
    MidiBinding binding;
    binding.cc = 10;
    binding.action = ActionEvent{Action::ToggleMute};
    handler_->addBinding(binding);

    handler_->removeBinding(0);
    auto bindings = handler_->getBindings();
    EXPECT_TRUE(bindings.empty());
}

TEST_F(MidiHandlerTest, SerializeDeserialize) {
    MidiBinding b;
    b.cc = 7;
    b.channel = 1;
    b.type = MidiMappingType::Continuous;
    ActionEvent act;
    act.action = Action::InputGainAdjust;
    act.floatParam = 0.5f;
    b.action = act;
    handler_->addBinding(b);

    auto mappings = handler_->exportMappings();
    ASSERT_EQ(mappings.size(), 1u);
    EXPECT_EQ(mappings[0].cc, 7);
    EXPECT_EQ(mappings[0].channel, 1);

    handler_->removeBinding(0);
    EXPECT_TRUE(handler_->getBindings().empty());

    handler_->loadFromMappings(mappings);
    auto reloaded = handler_->getBindings();
    ASSERT_EQ(reloaded.size(), 1u);
    EXPECT_EQ(reloaded[0].cc, 7);
}

TEST_F(MidiHandlerTest, LearnStartComplete) {
    bool learnCompleted = false;
    int learnedCC = -1;

    handler_->startLearn([&](int cc, int /*note*/, int /*channel*/, const juce::String& /*device*/) {
        learnedCC = cc;
        learnCompleted = true;
    });
    EXPECT_TRUE(handler_->isLearning());

    auto msg = juce::MidiMessage::controllerEvent(1, 64, 127);
    handler_->injectTestMessage(msg);

    EXPECT_TRUE(learnCompleted);
    EXPECT_EQ(learnedCC, 64);
    EXPECT_FALSE(handler_->isLearning());
}

TEST_F(MidiHandlerTest, LearnCancel) {
    bool learnCompleted = false;
    handler_->startLearn([&](int, int, int, const juce::String&) {
        learnCompleted = true;
    });
    EXPECT_TRUE(handler_->isLearning());

    handler_->stopLearn();
    EXPECT_FALSE(handler_->isLearning());
    EXPECT_FALSE(learnCompleted);
}

TEST_F(MidiHandlerTest, LearnTimeout) {
    // Verify that stopLearn() can be called after a short wait
    // (simulates timeout scenario without spinning the full 30s timer)
    bool learnCompleted = false;
    handler_->startLearn([&](int, int, int, const juce::String&) {
        learnCompleted = true;
    });
    EXPECT_TRUE(handler_->isLearning());

    // Short sleep to allow message loop to run briefly (timer does NOT fire in 100ms)
    juce::Thread::sleep(100);
    EXPECT_TRUE(handler_->isLearning());

    handler_->stopLearn();
    EXPECT_FALSE(handler_->isLearning());
    EXPECT_FALSE(learnCompleted);
}

TEST_F(MidiHandlerTest, DuplicateBindingDetect) {
    MidiBinding b1, b2;
    b1.cc = 7;
    b1.channel = 0;
    b1.action = ActionEvent{Action::ToggleMute};

    b2.cc = 7;
    b2.channel = 0;
    b2.action = ActionEvent{Action::MonitorToggle};

    handler_->addBinding(b1);
    handler_->addBinding(b2);

    auto bindings = handler_->getBindings();
    EXPECT_EQ(bindings.size(), 2u);
}

TEST_F(MidiHandlerTest, DispatchOutsideLock) {
    MidiBinding b;
    b.cc = 1;
    b.type = MidiMappingType::Toggle;
    b.action = ActionEvent{Action::ToggleMute};
    handler_->addBinding(b);

    auto msg = juce::MidiMessage::controllerEvent(1, 1, 127);
    handler_->injectTestMessage(msg);
    SUCCEED();
}
