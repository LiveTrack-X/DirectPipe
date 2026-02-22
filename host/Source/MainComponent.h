/**
 * @file MainComponent.h
 * @brief Main application component — combines audio engine and UI
 */
#pragma once

#include <JuceHeader.h>
#include "Audio/AudioEngine.h"
#include "UI/DeviceSelector.h"
#include "UI/PluginChainEditor.h"
#include "UI/LevelMeter.h"
#include "UI/PresetManager.h"
#include "UI/DirectPipeLookAndFeel.h"

#include <memory>

namespace directpipe {

/**
 * @brief Root UI component for the DirectPipe application.
 *
 * Hosts the audio engine and all UI sub-components:
 * - Device selector (input/output)
 * - VST plugin chain editor
 * - Level meters
 * - Output controls
 * - Status bar with latency/CPU info
 */
class MainComponent : public juce::Component,
                      public juce::Timer {
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;

    // Audio engine (core)
    AudioEngine audioEngine_;

    // Custom look and feel
    DirectPipeLookAndFeel lookAndFeel_;

    // UI Components
    std::unique_ptr<DeviceSelector> deviceSelector_;
    std::unique_ptr<PluginChainEditor> pluginChainEditor_;
    std::unique_ptr<LevelMeter> inputMeter_;
    std::unique_ptr<LevelMeter> outputMeterOBS_;
    std::unique_ptr<LevelMeter> outputMeterVMic_;
    std::unique_ptr<LevelMeter> outputMeterMonitor_;

    // Output volume sliders
    juce::Slider obsVolumeSlider_;
    juce::Slider vmicVolumeSlider_;
    juce::Slider monitorVolumeSlider_;

    // Output enable toggles
    juce::ToggleButton obsEnableBtn_{"OBS Plugin"};
    juce::ToggleButton vmicEnableBtn_{"Virtual Mic"};
    juce::ToggleButton monitorEnableBtn_{"Monitor"};

    // Status bar labels
    juce::Label latencyLabel_;
    juce::Label cpuLabel_;
    juce::Label formatLabel_;
    juce::Label obsStatusLabel_;

    // Section labels
    juce::Label inputSectionLabel_;
    juce::Label vstSectionLabel_;
    juce::Label outputSectionLabel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace directpipe
