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
    std::function<void(bool)> onIpcToggle;

    /** Set the IPC toggle state externally (e.g., when loading settings). */
    void setIpcToggleState(bool enabled);

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
    void onMonitorBufferSizeChanged();
    void onMonitorEnableToggled();
    void refreshBufferSizeCombo();

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
    juce::Label monitorBufferLabel_{"", "Buffer:"};
    juce::ComboBox monitorBufferCombo_;
    juce::ToggleButton monitorEnableButton_{"Enable"};
    juce::Label monitorStatusLabel_;

    // ── VST Receiver section ──
    juce::Label ipcHeaderLabel_{"", "VST Receiver (DirectPipe Receiver)"};
    juce::ToggleButton ipcToggle_{"Enable VST Receiver Output"};
    juce::Label ipcInfoLabel_{"", "Send processed audio to DirectPipe Receiver VST plugin."};

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

    // Separator line positions (set in resized, drawn in paint)
    int separatorY1_ = 0;
    int separatorY2_ = 0;

    static constexpr juce::uint32 kBgColour       = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour   = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour    = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour      = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour   = 0xFF8888AA;
    static constexpr juce::uint32 kRedColour       = 0xFFE05050;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputPanel)
};

} // namespace directpipe
