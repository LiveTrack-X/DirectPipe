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
#include "UI/DeviceSelector.h"
#include "UI/PluginChainEditor.h"
#include "UI/LevelMeter.h"
#include "UI/PresetManager.h"
#include "UI/DirectPipeLookAndFeel.h"
#include "UI/AudioSettings.h"
#include "UI/OutputPanel.h"
#include "UI/ControlSettingsPanel.h"

#include <memory>

namespace directpipe {

/**
 * @brief Root UI component for the DirectPipe v3.1 application.
 *
 * Hosts the audio engine, external control system, and all UI sub-components:
 * - Device selector (input)
 * - Audio settings (sample rate, buffer, channel mode)
 * - VST plugin chain editor
 * - Level meters
 * - Output panel (Virtual Loop Mic + Monitor)
 * - Control settings (Hotkeys, MIDI, Stream Deck)
 * - Status bar with latency/CPU info
 */
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

    /** @brief Refresh all UI components to match engine state. */
    void refreshUI();

private:
    void timerCallback() override;
    void saveSettings();
    void loadSettings();

    // Audio engine (core)
    AudioEngine audioEngine_;

    // External control system
    ActionDispatcher dispatcher_;
    StateBroadcaster broadcaster_;
    std::unique_ptr<ControlManager> controlManager_;

    // Custom look and feel
    DirectPipeLookAndFeel lookAndFeel_;

    // UI Components
    std::unique_ptr<DeviceSelector> deviceSelector_;
    std::unique_ptr<AudioSettings> audioSettings_;
    std::unique_ptr<PluginChainEditor> pluginChainEditor_;
    std::unique_ptr<LevelMeter> inputMeter_;
    std::unique_ptr<LevelMeter> outputMeter_;
    std::unique_ptr<OutputPanel> outputPanel_;
    std::unique_ptr<ControlSettingsPanel> controlSettingsPanel_;

    // Input gain slider
    juce::Slider inputGainSlider_;
    juce::Label inputGainLabel_{"", "Gain:"};

    // Preset buttons
    juce::TextButton savePresetBtn_{"Save Preset"};
    juce::TextButton loadPresetBtn_{"Load Preset"};
    std::unique_ptr<PresetManager> presetManager_;

    // Master mute button
    juce::TextButton panicMuteBtn_{"PANIC MUTE"};

    // Status bar labels
    juce::Label latencyLabel_;
    juce::Label cpuLabel_;
    juce::Label formatLabel_;
    juce::Label portableLabel_;

    // Section labels
    juce::Label inputSectionLabel_;
    juce::Label vstSectionLabel_;
    juce::Label outputSectionLabel_;
    juce::Label controlSectionLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace directpipe
