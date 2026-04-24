// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include <JuceHeader.h>
#include <gtest/gtest.h>
#include "Audio/AudioEngine.h"
#include "Audio/VSTChain.h"
#include "UI/PresetManager.h"
#include "UI/PresetSlotBar.h"
#include "UI/PluginChainEditor.h"
#include "Control/ActionHandler.h"

using namespace directpipe;

class ActionHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::MessageManager::getInstance();

        // Enable portable mode so presets are written to a temp dir
        auto exeDir = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory();
        portableFlag_ = exeDir.getChildFile("portable.flag");
        portableFlag_.create();
        configDir_ = exeDir.getChildFile("config");
        configDir_.createDirectory();

        engine_      = std::make_unique<AudioEngine>();
        presetMgr_   = std::make_unique<PresetManager>(*engine_);
        chainEditor_ = std::make_unique<PluginChainEditor>(engine_->getVSTChain());
        slotBar_     = std::make_unique<PresetSlotBar>(
                            *presetMgr_, *engine_, *chainEditor_,
                            loadingSlot_, partialLoad_);
        handler_     = std::make_unique<ActionHandler>(
                            *engine_, *presetMgr_, *slotBar_,
                            loadingSlot_, partialLoad_);
    }

    void TearDown() override {
        handler_.reset();
        slotBar_.reset();
        chainEditor_.reset();
        presetMgr_.reset();
        engine_.reset();
        configDir_.deleteRecursively();
        portableFlag_.deleteFile();
    }

    juce::File portableFlag_;
    juce::File configDir_;
    std::atomic<bool> loadingSlot_{false};
    std::atomic<bool> partialLoad_{false};
    std::unique_ptr<AudioEngine>       engine_;
    std::unique_ptr<PresetManager>     presetMgr_;
    std::unique_ptr<PluginChainEditor> chainEditor_;
    std::unique_ptr<PresetSlotBar>     slotBar_;
    std::unique_ptr<ActionHandler>     handler_;
};

// Test 1: Panic mute engage — engine becomes muted, callback fired
TEST_F(ActionHandlerTest, PanicMuteEngage) {
    bool callbackFired = false;
    handler_->onPanicStateChanged = [&](bool muted) {
        if (muted) callbackFired = true;
    };

    EXPECT_FALSE(engine_->isMuted());
    handler_->togglePanicMute();
    EXPECT_TRUE(engine_->isMuted());
    EXPECT_TRUE(callbackFired);
}

// Test 2: Panic mute restore — toggle twice, engine returns to unmuted
TEST_F(ActionHandlerTest, PanicMuteRestore) {
    handler_->togglePanicMute();
    EXPECT_TRUE(engine_->isMuted());

    handler_->togglePanicMute();
    EXPECT_FALSE(engine_->isMuted());
}

TEST_F(ActionHandlerTest, PanicMuteRestoresOutputMuteBeforeRelease) {
    engine_->setOutputMuted(true);

    handler_->togglePanicMute();
    EXPECT_TRUE(engine_->isMuted());
    EXPECT_TRUE(engine_->isOutputMuted());

    handler_->togglePanicMute();
    EXPECT_FALSE(engine_->isMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
}

TEST_F(ActionHandlerTest, PanicMuteRestoreFromSettingsKeepsPreMuteOutputMute) {
    engine_->setOutputMuted(true);
    engine_->setMuted(true);

    handler_->restorePanicMuteFromSettings();
    EXPECT_TRUE(engine_->isMuted());
    EXPECT_TRUE(engine_->isOutputMuted());

    handler_->togglePanicMute();
    EXPECT_FALSE(engine_->isMuted());
    EXPECT_TRUE(engine_->isOutputMuted());
}

// Test 3: Panic mute preserves monitor state — monitor enabled before panic is restored after
TEST_F(ActionHandlerTest, PanicMutePreserveMonitor) {
    // Enable monitor before panic
    engine_->setMonitorEnabled(true);
    engine_->getOutputRouter().setEnabled(OutputRouter::Output::Monitor, true);
    EXPECT_TRUE(engine_->isMonitorEnabled());

    handler_->togglePanicMute();
    // During panic, monitor should be disabled
    EXPECT_FALSE(engine_->isMonitorEnabled());

    handler_->togglePanicMute();
    // After panic restore, monitor should be re-enabled
    EXPECT_TRUE(engine_->isMonitorEnabled());
}

// Test 4: Callback order — onPanicStateChanged is called for both engage and disengage
TEST_F(ActionHandlerTest, CallbackOrder) {
    std::vector<bool> states;
    handler_->onPanicStateChanged = [&](bool muted) {
        states.push_back(muted);
    };

    handler_->togglePanicMute();
    handler_->togglePanicMute();

    ASSERT_EQ(states.size(), 2u);
    EXPECT_TRUE(states[0]);   // first call: muted = true
    EXPECT_FALSE(states[1]);  // second call: muted = false
}

// Test 5: handle(ToggleMute) with "output" param — output mute toggled
TEST_F(ActionHandlerTest, OutputMuteToggleViaHandle) {
    // Ensure engine is not in panic mute (so toggle works)
    EXPECT_FALSE(engine_->isMuted());
    EXPECT_FALSE(engine_->isOutputMuted());

    ActionEvent evt;
    evt.action = Action::ToggleMute;
    evt.stringParam = "output";
    handler_->handle(evt);

    EXPECT_TRUE(engine_->isOutputMuted());

    // Toggle back
    handler_->handle(evt);
    EXPECT_FALSE(engine_->isOutputMuted());
}

TEST_F(ActionHandlerTest, PanicMuteSetModeIgnoresDuplicateEngage) {
    engine_->setMonitorEnabled(true);
    engine_->getOutputRouter().setEnabled(OutputRouter::Output::Monitor, true);

    ActionEvent setOn;
    setOn.action = Action::PanicMute;
    setOn.stringParam = "set";
    setOn.intParam = 1;

    ActionEvent setOff;
    setOff.action = Action::PanicMute;
    setOff.stringParam = "set";
    setOff.intParam = 0;

    handler_->handle(setOn);
    EXPECT_TRUE(engine_->isMuted());

    // Duplicate "set on" should be ignored and must not overwrite restore snapshot.
    handler_->handle(setOn);
    EXPECT_TRUE(engine_->isMuted());

    handler_->handle(setOff);
    EXPECT_FALSE(engine_->isMuted());
    EXPECT_TRUE(engine_->isMonitorEnabled());
}
