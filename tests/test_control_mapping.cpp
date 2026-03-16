// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file test_control_mapping.cpp
 * @brief Unit tests for ControlMapping serialization/deserialization
 */

#include <gtest/gtest.h>
#include "Control/ControlMapping.h"
#include "Control/HotkeyHandler.h"

// Forward-declare MidiMappingType to avoid pulling in MidiHandler.h
// (which requires juce_audio_devices not linked in test target)
namespace directpipe {
    enum class MidiMappingType { Toggle = 0, Momentary, Continuous, NoteOnOff };
}

using namespace directpipe;

class ControlMappingTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use a temp directory for test files
        tempDir_ = juce::File::getSpecialLocation(
            juce::File::tempDirectory).getChildFile("directpipe_test_" +
                juce::String(juce::Random::getSystemRandom().nextInt()));
        tempDir_.createDirectory();
        testFile_ = tempDir_.getChildFile("test-controls.json");
    }

    void TearDown() override {
        tempDir_.deleteRecursively();
    }

    juce::File tempDir_;
    juce::File testFile_;
    ControlMappingStore store_;
};

// ─── Defaults ──────────────────────────────────────────────────────────────

TEST_F(ControlMappingTest, DefaultsHaveHotkeys) {
    auto config = ControlMappingStore::createDefaults();
    // 9 bypass (1-9) + master (0) + panic + input mute + output mute +
    // monitor + IPC + 5 slots = 21
    EXPECT_GE(config.hotkeys.size(), 5u);  // reduced defaults (11 conflict-free hotkeys)
}

TEST_F(ControlMappingTest, DefaultsHaveNoMidi) {
    auto config = ControlMappingStore::createDefaults();
    EXPECT_TRUE(config.midiMappings.empty());
}

TEST_F(ControlMappingTest, DefaultServerPorts) {
    auto config = ControlMappingStore::createDefaults();
    EXPECT_EQ(config.server.websocketPort, 8765);
    EXPECT_EQ(config.server.httpPort, 8766);
    EXPECT_TRUE(config.server.websocketEnabled);
    EXPECT_TRUE(config.server.httpEnabled);
}

TEST_F(ControlMappingTest, DefaultBypassHotkeys) {
    auto config = ControlMappingStore::createDefaults();
    // First hotkey should be Ctrl+Shift+M → Panic Mute (essential safety hotkey)
    ASSERT_FALSE(config.hotkeys.empty());
    auto& first = config.hotkeys[0];
    EXPECT_EQ(first.modifiers, static_cast<uint32_t>(HK_CTRL | HK_SHIFT));
    EXPECT_EQ(first.virtualKey, static_cast<uint32_t>('M'));
    EXPECT_EQ(first.action.action, Action::PanicMute);
    // Should also contain at least one plugin bypass hotkey
    bool foundBypass = false;
    for (const auto& hk : config.hotkeys)
        if (hk.action.action == Action::PluginBypass) { foundBypass = true; break; }
    EXPECT_TRUE(foundBypass);
}

TEST_F(ControlMappingTest, DefaultPresetSlotHotkeys) {
    auto config = ControlMappingStore::createDefaults();
    // Find a preset slot hotkey (F1 = 0x70)
    bool foundSlotA = false;
    for (const auto& hk : config.hotkeys) {
        if (hk.action.action == Action::SwitchPresetSlot && hk.action.intParam == 0) {
            EXPECT_EQ(hk.virtualKey, 0x70u); // VK_F1
            foundSlotA = true;
        }
    }
    EXPECT_TRUE(foundSlotA);
}

// ─── Save & Load Roundtrip ─────────────────────────────────────────────────

TEST_F(ControlMappingTest, SaveAndLoadRoundtrip) {
    auto original = ControlMappingStore::createDefaults();
    EXPECT_TRUE(store_.save(original, testFile_));
    EXPECT_TRUE(testFile_.existsAsFile());

    auto loaded = store_.load(testFile_);
    EXPECT_EQ(loaded.hotkeys.size(), original.hotkeys.size());
    EXPECT_EQ(loaded.server.websocketPort, original.server.websocketPort);
    EXPECT_EQ(loaded.server.httpPort, original.server.httpPort);
}

TEST_F(ControlMappingTest, HotkeyRoundtripPreservesAction) {
    ControlConfig config;
    HotkeyMapping hk;
    hk.modifiers = HK_CTRL | HK_ALT;
    hk.virtualKey = 'X';
    hk.action = {Action::PanicMute, 42, 0.75f, "test_param"};
    hk.displayName = "Ctrl+Alt+X";
    config.hotkeys.push_back(hk);

    EXPECT_TRUE(store_.save(config, testFile_));
    auto loaded = store_.load(testFile_);

    ASSERT_EQ(loaded.hotkeys.size(), 1u);
    auto& lhk = loaded.hotkeys[0];
    EXPECT_EQ(lhk.modifiers, static_cast<uint32_t>(HK_CTRL | HK_ALT));
    EXPECT_EQ(lhk.virtualKey, static_cast<uint32_t>('X'));
    EXPECT_EQ(lhk.action.action, Action::PanicMute);
    EXPECT_EQ(lhk.action.intParam, 42);
    EXPECT_NEAR(lhk.action.floatParam, 0.75f, 0.001f);
    EXPECT_EQ(lhk.action.stringParam, "test_param");
    EXPECT_EQ(lhk.displayName, "Ctrl+Alt+X");
}

TEST_F(ControlMappingTest, MidiRoundtripPreservesFields) {
    ControlConfig config;
    MidiMapping midi;
    midi.cc = 7;
    midi.note = -1;
    midi.channel = 3;
    midi.type = MidiMappingType::Continuous;
    midi.deviceName = "nanoKONTROL2";
    midi.action = {Action::SetVolume, 0, 0.5f, "monitor"};
    config.midiMappings.push_back(midi);

    EXPECT_TRUE(store_.save(config, testFile_));
    auto loaded = store_.load(testFile_);

    ASSERT_EQ(loaded.midiMappings.size(), 1u);
    auto& lm = loaded.midiMappings[0];
    EXPECT_EQ(lm.cc, 7);
    EXPECT_EQ(lm.note, -1);
    EXPECT_EQ(lm.channel, 3);
    EXPECT_EQ(static_cast<int>(lm.type), static_cast<int>(MidiMappingType::Continuous));
    EXPECT_EQ(lm.deviceName, "nanoKONTROL2");
    EXPECT_EQ(lm.action.action, Action::SetVolume);
    EXPECT_EQ(lm.action.stringParam, "monitor");
}

TEST_F(ControlMappingTest, ServerConfigRoundtrip) {
    ControlConfig config;
    config.server.websocketPort = 9000;
    config.server.websocketEnabled = false;
    config.server.httpPort = 9001;
    config.server.httpEnabled = true;

    EXPECT_TRUE(store_.save(config, testFile_));
    auto loaded = store_.load(testFile_);

    EXPECT_EQ(loaded.server.websocketPort, 9000);
    EXPECT_FALSE(loaded.server.websocketEnabled);
    EXPECT_EQ(loaded.server.httpPort, 9001);
    EXPECT_TRUE(loaded.server.httpEnabled);
}

TEST_F(ControlMappingTest, IntParam2Roundtrip) {
    ControlConfig config;
    HotkeyMapping hk;
    hk.modifiers = 0;
    hk.virtualKey = 'A';
    hk.action = {Action::SetPluginParameter, 2, 0.5f, "", 7};
    config.hotkeys.push_back(hk);

    EXPECT_TRUE(store_.save(config, testFile_));
    auto loaded = store_.load(testFile_);

    ASSERT_EQ(loaded.hotkeys.size(), 1u);
    EXPECT_EQ(loaded.hotkeys[0].action.intParam, 2);
    EXPECT_EQ(loaded.hotkeys[0].action.intParam2, 7);
    EXPECT_NEAR(loaded.hotkeys[0].action.floatParam, 0.5f, 0.001f);
}

// ─── Error Handling ────────────────────────────────────────────────────────

TEST_F(ControlMappingTest, LoadNonexistentReturnsDefaults) {
    auto nonexistent = tempDir_.getChildFile("does_not_exist.json");
    auto loaded = store_.load(nonexistent);
    // Should return defaults, not empty
    EXPECT_GE(loaded.hotkeys.size(), 5u);  // reduced defaults (11 conflict-free hotkeys)
}

TEST_F(ControlMappingTest, LoadInvalidJsonReturnsDefaults) {
    testFile_.replaceWithText("this is not json { garbage");
    auto loaded = store_.load(testFile_);
    EXPECT_GE(loaded.hotkeys.size(), 5u);  // reduced defaults (11 conflict-free hotkeys)
}

TEST_F(ControlMappingTest, LoadEmptyFileReturnsDefaults) {
    testFile_.replaceWithText("");
    auto loaded = store_.load(testFile_);
    EXPECT_GE(loaded.hotkeys.size(), 5u);  // reduced defaults (11 conflict-free hotkeys)
}

TEST_F(ControlMappingTest, LoadEmptyObjectReturnsEmptyConfig) {
    testFile_.replaceWithText("{}");
    auto loaded = store_.load(testFile_);
    // Empty JSON object = valid but empty config (not defaults)
    EXPECT_TRUE(loaded.hotkeys.empty());
}

// ─── Multiple Mappings ─────────────────────────────────────────────────────

TEST_F(ControlMappingTest, MultipleHotkeysRoundtrip) {
    ControlConfig config;
    for (int i = 0; i < 10; ++i) {
        HotkeyMapping hk;
        hk.modifiers = HK_CTRL;
        hk.virtualKey = '0' + static_cast<uint32_t>(i);
        hk.action = {Action::PluginBypass, i, 0.0f, ""};
        hk.displayName = "Ctrl+" + std::to_string(i);
        config.hotkeys.push_back(hk);
    }

    EXPECT_TRUE(store_.save(config, testFile_));
    auto loaded = store_.load(testFile_);
    EXPECT_EQ(loaded.hotkeys.size(), 10u);

    for (int i = 0; i < 10; ++i) {
        EXPECT_EQ(loaded.hotkeys[i].action.intParam, i);
        EXPECT_EQ(loaded.hotkeys[i].virtualKey, static_cast<uint32_t>('0' + i));
    }
}

TEST_F(ControlMappingTest, MixedHotkeyAndMidiRoundtrip) {
    ControlConfig config;

    HotkeyMapping hk;
    hk.modifiers = HK_SHIFT;
    hk.virtualKey = 'A';
    hk.action = {Action::PanicMute, 0, 0.0f, ""};
    config.hotkeys.push_back(hk);

    MidiMapping midi;
    midi.cc = 1;
    midi.channel = 1;
    midi.type = MidiMappingType::Toggle;
    midi.action = {Action::MonitorToggle, 0, 0.0f, ""};
    config.midiMappings.push_back(midi);

    EXPECT_TRUE(store_.save(config, testFile_));
    auto loaded = store_.load(testFile_);

    EXPECT_EQ(loaded.hotkeys.size(), 1u);
    EXPECT_EQ(loaded.midiMappings.size(), 1u);
    EXPECT_EQ(loaded.hotkeys[0].action.action, Action::PanicMute);
    EXPECT_EQ(loaded.midiMappings[0].action.action, Action::MonitorToggle);
}
