// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
//
// This file is part of DirectPipe.
//
// DirectPipe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// DirectPipe is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with DirectPipe. If not, see <https://www.gnu.org/licenses/>.

/**
 * @file MainComponent.h
 * @brief Main application component — combines audio engine, control system, and UI
 */
#pragma once

#include <JuceHeader.h>
#include "Audio/AudioEngine.h"
#include "Audio/OutputRouter.h"
#include "Control/ActionDispatcher.h"
#include "Control/StateBroadcaster.h"
#include "Control/ControlManager.h"
#include "UI/PluginChainEditor.h"
#include "UI/LevelMeter.h"
#include "UI/PresetManager.h"
#include "UI/DirectPipeLookAndFeel.h"
#include "UI/AudioSettings.h"
#include "UI/OutputPanel.h"
#include "UI/ControlSettingsPanel.h"
#include "UI/NotificationBar.h"
#include "UI/LogPanel.h"

#include <array>
#include <memory>
#include <thread>

namespace directpipe {

class MainComponent : public juce::Component,
                      public juce::Timer,
                      public ActionListener {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ActionListener
    void onAction(const ActionEvent& event) override;
    void handleAction(const ActionEvent& event);

    /** @brief Refresh all UI components to match engine state. */
    void refreshUI();

    /** @brief Get the state broadcaster (for tray tooltip etc.). */
    StateBroadcaster& getBroadcaster() { return broadcaster_; }

private:
    void timerCallback() override;
    void saveSettings();
    void loadSettings();
    void markSettingsDirty();

    // Audio engine (core)
    AudioEngine audioEngine_;

    // External control system
    ActionDispatcher dispatcher_;
    StateBroadcaster broadcaster_;
    std::unique_ptr<ControlManager> controlManager_;

    // Custom look and feel
    DirectPipeLookAndFeel lookAndFeel_;

    // UI Components
    std::unique_ptr<AudioSettings> audioSettings_;
    std::unique_ptr<PluginChainEditor> pluginChainEditor_;
    std::unique_ptr<LevelMeter> inputMeter_;
    std::unique_ptr<LevelMeter> outputMeter_;
    std::unique_ptr<OutputPanel> outputPanel_;
    OutputPanel* outputPanelPtr_ = nullptr;  // raw ptr (rightTabs_ owns the component)
    std::unique_ptr<ControlSettingsPanel> controlSettingsPanel_;

    // Right-column tabbed panel (Audio Settings / Output / Controls)
    std::unique_ptr<juce::TabbedComponent> rightTabs_;

    // Input gain slider
    juce::Slider inputGainSlider_;
    juce::Label inputGainLabel_{"", "Gain:"};

    // Preset buttons
    juce::TextButton savePresetBtn_{"Save Preset"};
    juce::TextButton loadPresetBtn_{"Load Preset"};
    std::unique_ptr<PresetManager> presetManager_;

    // Layout constants
    static constexpr int kDefaultWidth  = 800;
    static constexpr int kDefaultHeight = 700;
    static constexpr int kStatusBarHeight = 30;
    static constexpr int kSlotBtnGap = 4;
    static constexpr int kMeterWidth = 40;

    // Quick preset slot buttons (A..E)
    static constexpr int kNumPresetSlots = 5;
    std::array<std::unique_ptr<juce::TextButton>, 5> slotButtons_;
    void onSlotClicked(int slotIndex);
    void updateSlotButtonStates();
    void setSlotButtonsEnabled(bool enabled);

    // Mute indicators (clickable) + panic mute button
    juce::TextButton outputMuteBtn_{"OUT"};
    juce::TextButton monitorMuteBtn_{"MON"};
    juce::TextButton vstMuteBtn_{"VST"};
    juce::TextButton panicMuteBtn_{"PANIC MUTE"};

    // Cached mute states (avoid redundant repaints)
    bool cachedOutputMuted_ = false;
    bool cachedMonitorMuted_ = false;
    bool cachedVstEnabled_ = false;

    // Status bar labels
    juce::Label latencyLabel_;
    juce::Label cpuLabel_;
    juce::Label formatLabel_;
    juce::Label portableLabel_;
    juce::HyperlinkButton creditLink_{"", juce::URL("https://github.com/LiveTrack-X/DirectPipe")};

    // Section labels (left column only)
    juce::Label inputSectionLabel_;
    juce::Label vstSectionLabel_;

    // Dirty-flag auto-save (debounce: save ~1s after last change)
    bool settingsDirty_ = false;
    int dirtyCooldown_ = 0;   // ticks remaining before save (30Hz)
    std::atomic<bool> loadingSlot_ { false };

    // Panic mute: remember pre-mute state for restore on unmute
    bool preMuteMonitorEnabled_ = false;
    bool preMuteOutputMuted_ = false;
    bool preMuteVstEnabled_ = false;

    // Non-intrusive notification system
    NotificationBar notificationBar_;
    void showNotification(const juce::String& message, NotificationLevel level);

    // Update check — show "NEW" on credit label if newer release exists
    void checkForUpdate();
    std::thread updateCheckThread_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace directpipe
