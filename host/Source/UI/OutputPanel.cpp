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
 * @file OutputPanel.cpp
 * @brief Monitor output + Recording control panel implementation
 */

#include "OutputPanel.h"
#include "../Control/ControlMapping.h"

namespace directpipe {

OutputPanel::OutputPanel(AudioEngine& engine)
    : engine_(engine)
{
    // ── Monitor Output section ──
    titleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(titleLabel_);

    monitorDeviceLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(monitorDeviceLabel_);

    monitorDeviceCombo_.onChange = [this] { onMonitorDeviceSelected(); };
    addAndMakeVisible(monitorDeviceCombo_);

    monitorVolumeLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(monitorVolumeLabel_);

    monitorVolumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    monitorVolumeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    monitorVolumeSlider_.setRange(0.0, 100.0, 1.0);
    monitorVolumeSlider_.setValue(100.0, juce::dontSendNotification);
    monitorVolumeSlider_.setTextValueSuffix(" %");
    monitorVolumeSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(kAccentColour));
    monitorVolumeSlider_.setColour(juce::Slider::trackColourId, juce::Colour(kAccentColour).withAlpha(0.4f));
    monitorVolumeSlider_.setColour(juce::Slider::backgroundColourId, juce::Colour(kSurfaceColour).brighter(0.1f));
    monitorVolumeSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(kTextColour));
    monitorVolumeSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    monitorVolumeSlider_.onValueChange = [this] { onMonitorVolumeChanged(); };
    addAndMakeVisible(monitorVolumeSlider_);

    monitorBufferLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(monitorBufferLabel_);

    monitorBufferCombo_.onChange = [this] { onMonitorBufferSizeChanged(); };
    addAndMakeVisible(monitorBufferCombo_);

    monitorLatencyLabel_.setFont(juce::Font(11.0f));
    monitorLatencyLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(monitorLatencyLabel_);

    monitorEnableButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    monitorEnableButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    monitorEnableButton_.onClick = [this] { onMonitorEnableToggled(); };
    addAndMakeVisible(monitorEnableButton_);

    monitorStatusLabel_.setFont(juce::Font(11.0f));
    monitorStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF888888));
    addAndMakeVisible(monitorStatusLabel_);

    // ── VST Receiver section ──
    ipcHeaderLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    ipcHeaderLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(ipcHeaderLabel_);

    ipcToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    ipcToggle_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    ipcToggle_.onClick = [this] {
        if (onIpcToggle) onIpcToggle(ipcToggle_.getToggleState());
    };
    addAndMakeVisible(ipcToggle_);

    ipcInfoLabel_.setFont(juce::Font(11.0f));
    ipcInfoLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(ipcInfoLabel_);

    // ── Recording section ──
    recordingTitleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
    recordingTitleLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(recordingTitleLabel_);

    recordBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3A3A5A));
    recordBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    recordBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    recordBtn_.onClick = [this] {
        if (onRecordToggle) onRecordToggle();
    };
    addAndMakeVisible(recordBtn_);

    recordTimeLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
    recordTimeLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(recordTimeLabel_);

    playLastBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(kAccentColour));
    playLastBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    playLastBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    playLastBtn_.setEnabled(false);
    playLastBtn_.onClick = [this] {
        if (lastRecordedFile_.existsAsFile())
            lastRecordedFile_.startAsProcess();
    };
    addAndMakeVisible(playLastBtn_);

    openFolderBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3A3A5A));
    openFolderBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    openFolderBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    openFolderBtn_.onClick = [this] {
        if (recordingFolder_.exists())
            recordingFolder_.startAsProcess();
    };
    addAndMakeVisible(openFolderBtn_);
    openFolderBtn_.setEnabled(recordingFolder_.exists());

    changeFolderBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3A3A5A));
    changeFolderBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    changeFolderBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    changeFolderBtn_.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Select Recording Folder", recordingFolder_);
        auto safeThis = juce::Component::SafePointer<OutputPanel>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectDirectories,
                             [safeThis, chooser](const juce::FileChooser& fc) {
            if (!safeThis) return;
            auto result = fc.getResult();
            if (result.isDirectory()) {
                safeThis->setRecordingFolder(result);
                safeThis->saveRecordingConfig();
            }
        });
    };
    addAndMakeVisible(changeFolderBtn_);

    folderPathLabel_.setFont(juce::Font(10.0f));
    folderPathLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(folderPathLabel_);

    // Load recording folder config
    loadRecordingConfig();

    refreshDeviceLists();

    auto& router = engine_.getOutputRouter();
    monitorVolumeSlider_.setValue(
        static_cast<double>(router.getVolume(OutputRouter::Output::Monitor)) * 100.0,
        juce::dontSendNotification);
    monitorEnableButton_.setToggleState(
        router.isEnabled(OutputRouter::Output::Monitor),
        juce::dontSendNotification);

    startTimerHz(4);
}

OutputPanel::~OutputPanel()
{
    stopTimer();
}

void OutputPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));

    // Section background
    auto area = getLocalBounds().reduced(4);
    g.setColour(juce::Colour(kSurfaceColour));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);

    // Separator lines between sections
    auto bounds = getLocalBounds().reduced(12);
    g.setColour(juce::Colour(kDimTextColour).withAlpha(0.3f));
    if (separatorY1_ > 0)
        g.drawHorizontalLine(separatorY1_, static_cast<float>(bounds.getX()),
                             static_cast<float>(bounds.getRight()));
    if (separatorY2_ > 0)
        g.drawHorizontalLine(separatorY2_, static_cast<float>(bounds.getX()),
                             static_cast<float>(bounds.getRight()));
}

void OutputPanel::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    constexpr int rowH = 28;
    constexpr int gap  = 8;
    constexpr int labelW = 80;

    int y = bounds.getY();
    int w = bounds.getWidth();
    int x = bounds.getX();

    // ── Monitor Output ──
    titleLabel_.setBounds(x, y, w, rowH);
    y += rowH + gap;

    monitorDeviceLabel_.setBounds(x, y, labelW, rowH);
    monitorDeviceCombo_.setBounds(x + labelW + gap, y, w - labelW - gap, rowH);
    y += rowH + gap;

    monitorVolumeLabel_.setBounds(x, y, labelW, rowH);
    monitorVolumeSlider_.setBounds(x + labelW + gap, y, w - labelW - gap, rowH);
    y += rowH + gap;

    monitorBufferLabel_.setBounds(x, y, labelW, rowH);
    monitorBufferCombo_.setBounds(x + labelW + gap, y, w - labelW - gap, rowH);
    y += rowH + 2;

    monitorLatencyLabel_.setBounds(x + labelW + gap, y, w - labelW - gap, 16);
    y += 18 + gap;

    monitorEnableButton_.setBounds(x + labelW + gap, y, 120, rowH);
    y += rowH + gap;

    monitorStatusLabel_.setBounds(x, y, w, 18);
    y += 24;

    separatorY1_ = y - 4;

    // ── VST Receiver ──
    ipcHeaderLabel_.setBounds(x, y, w, rowH);
    y += rowH + gap;

    ipcToggle_.setBounds(x, y, w, rowH);
    y += rowH + gap;

    ipcInfoLabel_.setBounds(x, y, w, 18);
    y += 24;

    separatorY2_ = y - 4;

    // ── Recording ──
    recordingTitleLabel_.setBounds(x, y, w, rowH);
    y += rowH + gap;

    // Row: [REC 55] [time 55] [Play 50] [Open Folder flex] [... 30]
    int recBtnW = 55, timeLblW = 55, playW = 45, dotW = 30, btnGap = 4;
    int openW = w - recBtnW - timeLblW - playW - dotW - btnGap * 4;
    int bx = x;

    recordBtn_.setBounds(bx, y, recBtnW, rowH);
    bx += recBtnW + btnGap;
    recordTimeLabel_.setBounds(bx, y, timeLblW, rowH);
    bx += timeLblW + btnGap;
    playLastBtn_.setBounds(bx, y, playW, rowH);
    bx += playW + btnGap;
    openFolderBtn_.setBounds(bx, y, openW, rowH);
    bx += openW + btnGap;
    changeFolderBtn_.setBounds(bx, y, dotW, rowH);
    y += rowH + 4;

    folderPathLabel_.setBounds(x, y, w, 16);
}

void OutputPanel::timerCallback()
{
    auto& router = engine_.getOutputRouter();
    bool monEnabled = router.isEnabled(OutputRouter::Output::Monitor);
    if (monitorEnableButton_.getToggleState() != monEnabled)
        monitorEnableButton_.setToggleState(monEnabled, juce::dontSendNotification);

    // Sync volume slider with actual router value (external control may change it)
    double actualVol = static_cast<double>(router.getVolume(OutputRouter::Output::Monitor)) * 100.0;
    if (std::abs(monitorVolumeSlider_.getValue() - actualVol) > 0.5)
        monitorVolumeSlider_.setValue(actualVol, juce::dontSendNotification);

    // Update monitor latency display (only when Active, using monitor's own SR)
    {
        auto& monOut = engine_.getMonitorOutput();
        if (monOut.getStatus() == VirtualCableStatus::Active) {
            double sr = monOut.getActualSampleRate();
            int bs = monOut.getActualBufferSize();
            if (sr > 0.0 && bs > 0) {
                double ms = (static_cast<double>(bs) / sr) * 1000.0;
                monitorLatencyLabel_.setText(
                    juce::String(ms, 2) + " ms  (" + juce::String(bs) + " samples @ "
                        + juce::String(static_cast<int>(sr)) + " Hz)",
                    juce::dontSendNotification);
            } else {
                monitorLatencyLabel_.setText("", juce::dontSendNotification);
            }
        } else {
            monitorLatencyLabel_.setText("", juce::dontSendNotification);
        }
    }

    // Show monitor device status
    auto& monOut = engine_.getMonitorOutput();
    auto status = monOut.getStatus();
    if (status == VirtualCableStatus::Active) {
        monitorStatusLabel_.setText("Active: " + monOut.getDeviceName(), juce::dontSendNotification);
        monitorStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF4CAF50));
    } else if (status == VirtualCableStatus::SampleRateMismatch) {
        double expected = engine_.getMonitorOutput().getActualSampleRate();
        monitorStatusLabel_.setText(
            "Error: sample rate mismatch (" + juce::String(static_cast<int>(expected)) + "Hz)",
            juce::dontSendNotification);
        monitorStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFCC8844));
    } else if (status == VirtualCableStatus::Error) {
        monitorStatusLabel_.setText("Error: device unavailable", juce::dontSendNotification);
        monitorStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFE05050));
    } else {
        if (monEnabled)
            monitorStatusLabel_.setText("No device selected - using main output", juce::dontSendNotification);
        else
            monitorStatusLabel_.setText("", juce::dontSendNotification);
        monitorStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF888888));
    }
}

void OutputPanel::updateRecordingState(bool isRecording, double seconds)
{
    recordBtn_.setButtonText(isRecording ? "STOP" : "REC");
    recordBtn_.setColour(juce::TextButton::buttonColourId,
        juce::Colour(isRecording ? kRedColour : 0xFF3A3A5Au));

    if (isRecording) {
        int secs = static_cast<int>(seconds);
        int mm = secs / 60;
        int ss = secs % 60;
        recordTimeLabel_.setText(
            juce::String(mm).paddedLeft('0', 2) + ":" + juce::String(ss).paddedLeft('0', 2),
            juce::dontSendNotification);
        recordTimeLabel_.setColour(juce::Label::textColourId, juce::Colour(kRedColour));
        playLastBtn_.setEnabled(false);
    } else {
        recordTimeLabel_.setText("", juce::dontSendNotification);
        playLastBtn_.setEnabled(lastRecordedFile_.existsAsFile());
    }
}

void OutputPanel::setLastRecordedFile(const juce::File& file)
{
    lastRecordedFile_ = file;
    playLastBtn_.setEnabled(file.existsAsFile());
}

void OutputPanel::setRecordingFolder(const juce::File& folder)
{
    recordingFolder_ = folder;
    folderPathLabel_.setText(folder.getFullPathName(), juce::dontSendNotification);
    openFolderBtn_.setEnabled(folder.exists());
}

void OutputPanel::saveRecordingConfig()
{
    auto configDir = ControlMappingStore::getConfigDirectory();
    auto configFile = configDir.getChildFile("recording-config.json");
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("recordingFolder", recordingFolder_.getFullPathName());
    auto json = juce::JSON::toString(juce::var(obj.get()));
    configFile.replaceWithText(json);
}

void OutputPanel::loadRecordingConfig()
{
    auto defaultFolder = juce::File::getSpecialLocation(
        juce::File::userDocumentsDirectory).getChildFile("DirectPipe Recordings");

    auto configDir = ControlMappingStore::getConfigDirectory();
    auto configFile = configDir.getChildFile("recording-config.json");

    if (configFile.existsAsFile()) {
        auto parsed = juce::JSON::parse(configFile.loadFileAsString());
        if (auto* obj = parsed.getDynamicObject()) {
            auto folderPath = obj->getProperty("recordingFolder").toString();
            if (folderPath.isNotEmpty()) {
                setRecordingFolder(juce::File(folderPath));
                return;
            }
        }
    }

    setRecordingFolder(defaultFolder);
}

void OutputPanel::refreshDeviceLists()
{
    monitorDeviceCombo_.clear(juce::dontSendNotification);
    auto devices = engine_.getMonitorOutput().getAvailableOutputDevices();
    for (int i = 0; i < devices.size(); ++i)
        monitorDeviceCombo_.addItem(devices[i], i + 1);

    auto currentDevice = engine_.getMonitorDeviceName();
    int idx = devices.indexOf(currentDevice);
    if (idx >= 0)
        monitorDeviceCombo_.setSelectedId(idx + 1, juce::dontSendNotification);

    refreshBufferSizeCombo();
}

void OutputPanel::onMonitorDeviceSelected()
{
    auto selectedText = monitorDeviceCombo_.getText();
    if (selectedText.isNotEmpty()) {
        engine_.setMonitorDevice(selectedText);
        refreshBufferSizeCombo();
        if (onSettingsChanged) onSettingsChanged();
    }
}

void OutputPanel::onMonitorVolumeChanged()
{
    float volume = static_cast<float>(monitorVolumeSlider_.getValue()) / 100.0f;
    engine_.getOutputRouter().setVolume(OutputRouter::Output::Monitor, volume);
    if (onSettingsChanged) onSettingsChanged();
}

void OutputPanel::onMonitorBufferSizeChanged()
{
    auto text = monitorBufferCombo_.getText();
    int bufferSize = text.getIntValue();
    if (bufferSize > 0) {
        engine_.setMonitorBufferSize(bufferSize);

        // Update combo to show the actual buffer size WASAPI applied
        int actual = engine_.getMonitorOutput().getActualBufferSize();
        if (actual > 0 && actual != bufferSize) {
            refreshBufferSizeCombo();
        }

        if (onSettingsChanged) onSettingsChanged();
    }
}

void OutputPanel::onMonitorEnableToggled()
{
    bool enabled = monitorEnableButton_.getToggleState();
    engine_.getOutputRouter().setEnabled(OutputRouter::Output::Monitor, enabled);
    engine_.setMonitorEnabled(enabled);
    if (onSettingsChanged) onSettingsChanged();
}

void OutputPanel::refreshBufferSizeCombo()
{
    monitorBufferCombo_.clear(juce::dontSendNotification);

    auto& monOut = engine_.getMonitorOutput();
    auto available = monOut.getAvailableBufferSizes();

    if (available.isEmpty()) {
        // Fallback: show common sizes when device is not yet initialized
        static const int fallback[] = { 64, 128, 256, 480, 512, 1024 };
        for (int i = 0; i < 6; ++i)
            monitorBufferCombo_.addItem(juce::String(fallback[i]) + " samples", i + 1);
    } else {
        for (int i = 0; i < available.size(); ++i)
            monitorBufferCombo_.addItem(juce::String(available[i]) + " samples", i + 1);
    }

    // Select the actual buffer size (what WASAPI is really using)
    int actual = monOut.getActualBufferSize();
    if (actual <= 0)
        actual = engine_.getMonitorBufferSize();  // preferred if not yet started

    for (int i = 0; i < monitorBufferCombo_.getNumItems(); ++i) {
        if (monitorBufferCombo_.getItemText(i).getIntValue() == actual) {
            monitorBufferCombo_.setSelectedItemIndex(i, juce::dontSendNotification);
            return;
        }
    }

    // If actual size isn't in the list, select the closest match
    if (monitorBufferCombo_.getNumItems() > 0)
        monitorBufferCombo_.setSelectedItemIndex(0, juce::dontSendNotification);
}

void OutputPanel::setIpcToggleState(bool enabled)
{
    ipcToggle_.setToggleState(enabled, juce::dontSendNotification);
}

} // namespace directpipe
