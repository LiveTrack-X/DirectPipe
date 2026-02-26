/**
 * @file OutputPanel.h
 * @brief Monitor output control panel
 *
 * Monitor: device selector, volume, enable toggle
 * (Main output is controlled via AudioSettings Output dropdown)
 */
#pragma once

#include <JuceHeader.h>
#include "../Audio/AudioEngine.h"

namespace directpipe {

class OutputPanel : public juce::Component,
                    public juce::Timer {
public:
    explicit OutputPanel(AudioEngine& engine);
    ~OutputPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void refreshDeviceLists();

    std::function<void()> onSettingsChanged;
    std::function<void()> onRecordToggle;

    /** Update recording state display (called from MainComponent timer). */
    void updateRecordingState(bool isRecording, double seconds);

    /** Set the last recorded file (for Play button). */
    void setLastRecordedFile(const juce::File& file);

    /** Get the current recording folder. */
    juce::File getRecordingFolder() const { return recordingFolder_; }

    /** Set the recording folder (e.g., from loaded config). */
    void setRecordingFolder(const juce::File& folder);

private:
    void timerCallback() override;

    void onMonitorDeviceSelected();
    void onMonitorVolumeChanged();
    void onMonitorEnableToggled();

    /** Save recording folder to config file. */
    void saveRecordingConfig();
    /** Load recording folder from config file. */
    void loadRecordingConfig();

    AudioEngine& engine_;

    // ── Monitor section ──
    juce::Label titleLabel_{"", "Monitor Output"};

    juce::Label monitorDeviceLabel_{"", "Device:"};
    juce::ComboBox monitorDeviceCombo_;
    juce::Slider monitorVolumeSlider_;
    juce::Label monitorVolumeLabel_{"", "Volume:"};
    juce::ToggleButton monitorEnableButton_{"Enable"};
    juce::Label monitorStatusLabel_;

    // ── Recording section ──
    juce::Label recordingTitleLabel_{"", "Recording"};
    juce::TextButton recordBtn_{"REC"};
    juce::Label recordTimeLabel_;
    juce::TextButton playLastBtn_{"Play"};
    juce::TextButton openFolderBtn_{"Open Folder"};
    juce::TextButton changeFolderBtn_{"..."};
    juce::Label folderPathLabel_;
    juce::File recordingFolder_;
    juce::File lastRecordedFile_;

    static constexpr juce::uint32 kBgColour       = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour    = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour      = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour   = 0xFF8888AA;
    static constexpr juce::uint32 kRedColour       = 0xFFE05050;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputPanel)
};

} // namespace directpipe
