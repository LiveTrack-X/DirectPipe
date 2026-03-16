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
 * @file AudioSettings.cpp
 * @brief Unified audio I/O configuration panel implementation
 */

#include "AudioSettings.h"

namespace directpipe {

// ─── Construction / Destruction ─────────────────────────────────────────────

AudioSettings::AudioSettings(AudioEngine& engine)
    : engine_(engine)
{
    // ── Title ──
    titleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(titleLabel_);

    auto styleLabel = [this](juce::Label& label) {
        label.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
        addAndMakeVisible(label);
    };

    auto styleCombo = [this](juce::ComboBox& combo) {
        addAndMakeVisible(combo);
    };

    // ── Driver type ──
    styleLabel(driverLabel_);
    styleCombo(driverCombo_);
    {
        auto types = engine_.getAvailableDeviceTypes();
        for (int i = 0; i < types.size(); ++i)
            driverCombo_.addItem(types[i], i + 1);

        // Select current type
        auto currentType = engine_.getCurrentDeviceType();
        int idx = types.indexOf(currentType);
        if (idx >= 0)
            driverCombo_.setSelectedId(idx + 1, juce::dontSendNotification);
        else if (types.size() > 0)
            driverCombo_.setSelectedId(1, juce::dontSendNotification);
    }
    driverCombo_.onChange = [this] { onDriverTypeChanged(); };

    // ── Input device ──
    styleLabel(inputLabel_);
    styleCombo(inputCombo_);
    inputCombo_.onChange = [this] { onInputDeviceChanged(); };

    // ── Output device ──
    styleLabel(outputLabel_);
    styleCombo(outputCombo_);
    outputCombo_.onChange = [this] { onOutputDeviceChanged(); };

    // ── ASIO channel selection ──
    styleLabel(inputChLabel_);
    styleCombo(inputChCombo_);
    inputChCombo_.onChange = [this] { onInputChannelChanged(); };

    styleLabel(outputChLabel_);
    styleCombo(outputChCombo_);
    outputChCombo_.onChange = [this] { onOutputChannelChanged(); };

    // ── Sample rate ──
    styleLabel(sampleRateLabel_);
    styleCombo(sampleRateCombo_);
    sampleRateCombo_.onChange = [this] { onSampleRateChanged(); };

    // ── Buffer size ──
    styleLabel(bufferSizeLabel_);
    styleCombo(bufferSizeCombo_);
    bufferSizeCombo_.onChange = [this] { onBufferSizeChanged(); };

    // ── Channel mode (radio group) ──
    styleLabel(channelModeLabel_);

    monoButton_.setRadioGroupId(1);
    stereoButton_.setRadioGroupId(1);
    stereoButton_.setToggleState(true, juce::dontSendNotification);
    monoButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    monoButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    stereoButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    stereoButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    monoButton_.onClick   = [this] { onChannelModeChanged(); };
    stereoButton_.onClick = [this] { onChannelModeChanged(); };
    addAndMakeVisible(monoButton_);
    addAndMakeVisible(stereoButton_);

    channelModeDescLabel_.setFont(juce::Font(11.0f));
    channelModeDescLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(channelModeDescLabel_);
    updateChannelModeDescription();

    // ── Output Volume ──
    styleLabel(outputVolumeLabel_);
    outputVolumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    outputVolumeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    outputVolumeSlider_.setRange(0.0, 100.0, 1.0);
    outputVolumeSlider_.setValue(100.0, juce::dontSendNotification);
    outputVolumeSlider_.setTextValueSuffix(" %");
    outputVolumeSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(kAccentColour));
    outputVolumeSlider_.setColour(juce::Slider::trackColourId, juce::Colour(kAccentColour).withAlpha(0.4f));
    outputVolumeSlider_.setColour(juce::Slider::backgroundColourId, juce::Colour(kSurfaceColour).brighter(0.1f));
    outputVolumeSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(kTextColour));
    outputVolumeSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    outputVolumeSlider_.onValueChange = [this] { onOutputVolumeChanged(); };
    addAndMakeVisible(outputVolumeSlider_);

    // ── Latency display ──
    latencyTitleLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    latencyTitleLabel_.setFont(juce::Font(13.0f));
    addAndMakeVisible(latencyTitleLabel_);

    latencyValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    latencyValueLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    addAndMakeVisible(latencyValueLabel_);

    // ── ASIO Control Panel button ──
    asioControlBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour).brighter(0.15f));
    asioControlBtn_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    asioControlBtn_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    asioControlBtn_.onClick = [this] { engine_.showAsioControlPanel(); };
    addAndMakeVisible(asioControlBtn_);

    // Listen for device changes
    engine_.getDeviceManager().addChangeListener(this);

    // Refresh device list when combos are clicked (scan before popup opens)
    inputCombo_.addMouseListener(this, true);
    outputCombo_.addMouseListener(this, true);

    // Synchronise the UI with current engine state
    refreshFromEngine();
}

AudioSettings::~AudioSettings()
{
    inputCombo_.removeMouseListener(this);
    outputCombo_.removeMouseListener(this);
    engine_.getDeviceManager().removeChangeListener(this);
}

// ─── Paint ──────────────────────────────────────────────────────────────────

void AudioSettings::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));

    auto area = getLocalBounds().reduced(4);
    g.setColour(juce::Colour(kSurfaceColour));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
}

// ─── Layout ─────────────────────────────────────────────────────────────────

void AudioSettings::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    constexpr int rowH = 28;
    constexpr int gap  = 8;
    constexpr int labelW = 120;

    int y = bounds.getY();

    // Title
    titleLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    // Driver type
    driverLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    driverCombo_.setBounds(bounds.getX() + labelW + gap, y,
                           bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Input device (always visible)
    inputLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    inputCombo_.setBounds(bounds.getX() + labelW + gap, y,
                          bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Output device (always visible)
    outputLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    outputCombo_.setBounds(bounds.getX() + labelW + gap, y,
                           bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // ASIO channel selection (only visible in ASIO mode)
    bool asio = isAsioMode();
    if (asio) {
        inputChLabel_.setBounds(bounds.getX(), y, labelW, rowH);
        inputChCombo_.setBounds(bounds.getX() + labelW + gap, y,
                                bounds.getWidth() - labelW - gap, rowH);
        y += rowH + gap;

        outputChLabel_.setBounds(bounds.getX(), y, labelW, rowH);
        outputChCombo_.setBounds(bounds.getX() + labelW + gap, y,
                                 bounds.getWidth() - labelW - gap, rowH);
        y += rowH + gap;

        inputChLabel_.setVisible(true);
        inputChCombo_.setVisible(true);
        outputChLabel_.setVisible(true);
        outputChCombo_.setVisible(true);
    } else {
        inputChLabel_.setVisible(false);
        inputChCombo_.setVisible(false);
        outputChLabel_.setVisible(false);
        outputChCombo_.setVisible(false);
    }

    // Sample Rate
    sampleRateLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    sampleRateCombo_.setBounds(bounds.getX() + labelW + gap, y,
                               bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Buffer Size
    bufferSizeLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    bufferSizeCombo_.setBounds(bounds.getX() + labelW + gap, y,
                               bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Channel Mode
    channelModeLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    int radioW = (bounds.getWidth() - labelW - gap) / 2;
    monoButton_.setBounds(bounds.getX() + labelW + gap, y, radioW, rowH);
    stereoButton_.setBounds(bounds.getX() + labelW + gap + radioW, y, radioW, rowH);
    y += rowH + gap + 4;

    // Channel mode description
    channelModeDescLabel_.setBounds(bounds.getX() + labelW + gap, y,
                                    bounds.getWidth() - labelW - gap, 18);
    y += 22;

    // Output Volume
    outputVolumeLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    outputVolumeSlider_.setBounds(bounds.getX() + labelW + gap, y,
                                  bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Latency display
    latencyTitleLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    latencyValueLabel_.setBounds(bounds.getX() + labelW + gap, y,
                                 bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // ASIO Control Panel button (only visible in ASIO mode)
    if (asio) {
        asioControlBtn_.setBounds(bounds.getX() + labelW + gap, y,
                                  bounds.getWidth() - labelW - gap, rowH);
        asioControlBtn_.setVisible(true);
    } else {
        asioControlBtn_.setVisible(false);
    }
}

// ─── Refresh from engine ────────────────────────────────────────────────────

void AudioSettings::refreshFromEngine()
{
    // Driver type
    auto currentType = engine_.getCurrentDeviceType();
    auto types = engine_.getAvailableDeviceTypes();
    int typeIdx = types.indexOf(currentType);
    if (typeIdx >= 0)
        driverCombo_.setSelectedId(typeIdx + 1, juce::dontSendNotification);

    // Rebuild device lists
    rebuildDeviceLists();
    rebuildSampleRateList();
    rebuildBufferSizeList();

    // Channel mode
    int ch = engine_.getChannelMode();
    if (ch == 2)
        stereoButton_.setToggleState(true, juce::dontSendNotification);
    else
        monoButton_.setToggleState(true, juce::dontSendNotification);

    updateChannelModeDescription();
    updateLatencyDisplay();

    // Output volume sync
    outputVolumeSlider_.setValue(
        static_cast<double>(engine_.getOutputRouter().getVolume(OutputRouter::Output::Main)) * 100.0,
        juce::dontSendNotification);

    // Update ASIO-specific visibility
    bool asio = isAsioMode();
    asioControlBtn_.setVisible(asio);
    inputChLabel_.setVisible(asio);
    inputChCombo_.setVisible(asio);
    outputChLabel_.setVisible(asio);
    outputChCombo_.setVisible(asio);
    resized();
}

// ─── ChangeListener ─────────────────────────────────────────────────────────

void AudioSettings::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    // Device manager changed — rebuild all lists (handles device plug/unplug)
    rebuildDeviceLists();
    rebuildSampleRateList();
    rebuildBufferSizeList();
    updateLatencyDisplay();

    // Sync output volume (may have changed via external control)
    double actualOutVol = static_cast<double>(engine_.getOutputRouter().getVolume(OutputRouter::Output::Main)) * 100.0;
    if (std::abs(outputVolumeSlider_.getValue() - actualOutVol) > 0.5)
        outputVolumeSlider_.setValue(actualOutVol, juce::dontSendNotification);
}

void AudioSettings::mouseDown(const juce::MouseEvent& event)
{
    // Only rescan when clicking on device combos (not panel background).
    // mouseDown fires before ComboBox::mouseUp opens the popup.
    auto* src = event.eventComponent;
    if (src == &inputCombo_  || inputCombo_.isParentOf(src) ||
        src == &outputCombo_ || outputCombo_.isParentOf(src))
    {
        if (auto* type = engine_.getDeviceManager().getCurrentDeviceTypeObject())
            type->scanForDevices();
        rebuildDeviceLists();
    }
}

// ─── Callbacks ──────────────────────────────────────────────────────────────

void AudioSettings::onDriverTypeChanged()
{
    int id = driverCombo_.getSelectedId();
    auto types = engine_.getAvailableDeviceTypes();
    if (id >= 1 && id <= types.size()) {
        auto result = engine_.setAudioDeviceType(types[id - 1]);

        // If switch failed, sync combo to actual current driver type
        if (!result) {
            auto actualType = engine_.getCurrentDeviceType();
            int actualIdx = types.indexOf(actualType);
            if (actualIdx >= 0)
                driverCombo_.setSelectedId(actualIdx + 1, juce::dontSendNotification);
            if (onError) onError(result.message);
        }

        rebuildDeviceLists();
        rebuildSampleRateList();
        rebuildBufferSizeList();
        updateLatencyDisplay();

        bool asio = isAsioMode();
        asioControlBtn_.setVisible(asio);
        inputChLabel_.setVisible(asio);
        inputChCombo_.setVisible(asio);
        outputChLabel_.setVisible(asio);
        outputChCombo_.setVisible(asio);
        resized();

        if (onSettingsChanged) onSettingsChanged();
    }
}

void AudioSettings::onInputDeviceChanged()
{
    auto selectedText = inputCombo_.getText();
    if (selectedText.isEmpty() || selectedText.contains("(Disconnected)")) return;

    if (isAsioMode()) {
        auto r = engine_.setAsioDevice(selectedText);
        if (!r && onError) onError(r.message);

        // Sync output combo to match (ASIO single device)
        auto outputs = engine_.getAvailableOutputDevices();
        int outIdx = outputs.indexOf(selectedText);
        if (outIdx >= 0)
            outputCombo_.setSelectedId(outIdx + 1, juce::dontSendNotification);

        rebuildChannelLists();
    } else {
        auto r = engine_.setInputDevice(selectedText);
        if (!r && onError) onError(r.message);
    }

    rebuildSampleRateList();
    rebuildBufferSizeList();
    updateLatencyDisplay();

    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onOutputDeviceChanged()
{
    auto selectedText = outputCombo_.getText();
    if (selectedText.isEmpty() || selectedText.contains("(Disconnected)")) return;

    // "None" = mute main output (keep device open for input)
    if (selectedText == "None") {
        engine_.setOutputNone(true);
        if (onSettingsChanged) onSettingsChanged();
        return;
    }

    // Switching away from None — unmute
    if (engine_.isOutputNone()) {
        engine_.setOutputNone(false);
    }

    // User manually selected a device — clear device-loss auto-mute
    if (engine_.isOutputAutoMuted()) {
        engine_.clearOutputAutoMute();
        engine_.setOutputMuted(false);
    }

    if (isAsioMode()) {
        auto r = engine_.setAsioDevice(selectedText);
        if (!r && onError) onError(r.message);

        // Sync input combo to match (ASIO single device)
        auto inputs = engine_.getAvailableInputDevices();
        int inIdx = inputs.indexOf(selectedText);
        if (inIdx >= 0)
            inputCombo_.setSelectedId(inIdx + 1, juce::dontSendNotification);

        rebuildChannelLists();
    } else {
        auto r = engine_.setOutputDevice(selectedText);
        if (!r && onError) onError(r.message);
    }

    rebuildSampleRateList();
    rebuildBufferSizeList();
    updateLatencyDisplay();

    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onInputChannelChanged()
{
    int id = inputChCombo_.getSelectedId();
    if (id < 1) return;

    int firstChannel = (id - 1) * 2;  // pairs: 0-1, 2-3, 4-5, ...
    int numCh = stereoButton_.getToggleState() ? 2 : 1;
    int totalChannels = engine_.getInputChannelNames().size();
    if (totalChannels > 0 && firstChannel + numCh > totalChannels)
        return;
    (void)engine_.setActiveInputChannels(firstChannel, numCh);

    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onOutputChannelChanged()
{
    int id = outputChCombo_.getSelectedId();
    if (id < 1) return;

    int firstChannel = (id - 1) * 2;
    int numCh = stereoButton_.getToggleState() ? 2 : 1;
    int totalChannels = engine_.getOutputChannelNames().size();
    if (totalChannels > 0 && firstChannel + numCh > totalChannels)
        return;
    (void)engine_.setActiveOutputChannels(firstChannel, numCh);

    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onSampleRateChanged()
{
    int id = sampleRateCombo_.getSelectedId();
    if (id < 1) return;

    auto rates = engine_.getAvailableSampleRates();
    if (id - 1 < rates.size()) {
        auto r = engine_.setSampleRate(rates[id - 1]);
        if (!r && onError) onError(r.message);
        updateLatencyDisplay();

        if (onSettingsChanged) onSettingsChanged();
    }
}

void AudioSettings::onBufferSizeChanged()
{
    int id = bufferSizeCombo_.getSelectedId();
    if (id < 1) return;

    auto sizes = engine_.getAvailableBufferSizes();
    if (id - 1 < sizes.size()) {
        auto r = engine_.setBufferSize(sizes[id - 1]);
        if (!r && onError) onError(r.message);
        updateLatencyDisplay();

        if (onSettingsChanged) onSettingsChanged();
    }
}

void AudioSettings::onChannelModeChanged()
{
    int channels = stereoButton_.getToggleState() ? 2 : 1;
    engine_.setChannelMode(channels);
    updateChannelModeDescription();

    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onOutputVolumeChanged()
{
    float volume = static_cast<float>(outputVolumeSlider_.getValue()) / 100.0f;
    engine_.getOutputRouter().setVolume(OutputRouter::Output::Main, volume);
    if (onSettingsChanged) onSettingsChanged();
}

// ─── Helpers ────────────────────────────────────────────────────────────────

bool AudioSettings::isAsioMode() const
{
    return engine_.getCurrentDeviceType().containsIgnoreCase("ASIO");
}

void AudioSettings::rebuildDeviceLists()
{
    // Input devices (from current driver type)
    inputCombo_.clear(juce::dontSendNotification);
    auto inputs = engine_.getAvailableInputDevices();
    for (int i = 0; i < inputs.size(); ++i)
        inputCombo_.addItem(inputs[i], i + 1);

    // Output devices (from current driver type)
    outputCombo_.clear(juce::dontSendNotification);
    if (!isAsioMode())
        outputCombo_.addItem("None", 1);
    auto outputs = engine_.getAvailableOutputDevices();
    int outputIdOffset = isAsioMode() ? 1 : 2;  // "None" takes ID 1 in WASAPI mode
    for (int i = 0; i < outputs.size(); ++i)
        outputCombo_.addItem(outputs[i], i + outputIdOffset);

    // Select current devices
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    engine_.getDeviceManager().getAudioDeviceSetup(setup);

    // Input selection — show "(Disconnected)" when input device is lost
    if (engine_.isInputDeviceLost() && !isAsioMode()) {
        auto desired = engine_.getDesiredInputDevice();
        if (desired.isNotEmpty() && !inputs.contains(desired)) {
            int dcId = inputs.size() + 1;
            inputCombo_.addItem(desired + " (Disconnected)", dcId);
            inputCombo_.setSelectedId(dcId, juce::dontSendNotification);
        } else {
            // Desired device is back in list but flag not yet cleared
            int inIdx = inputs.indexOf(desired);
            if (inIdx >= 0)
                inputCombo_.setSelectedId(inIdx + 1, juce::dontSendNotification);
        }
    } else {
        juce::String inputName = setup.inputDeviceName;
        if (inputName.isEmpty()) {
            if (auto* device = engine_.getDeviceManager().getCurrentAudioDevice())
                inputName = device->getName();
        }
        int inIdx = inputs.indexOf(inputName);
        if (inIdx >= 0)
            inputCombo_.setSelectedId(inIdx + 1, juce::dontSendNotification);
        else if (inputs.size() > 0)
            inputCombo_.setSelectedId(1, juce::dontSendNotification);
    }

    // Output selection
    if (engine_.isOutputNone() && !isAsioMode()) {
        outputCombo_.setSelectedId(1, juce::dontSendNotification); // "None" (intentional)
    } else if (engine_.isOutputAutoMuted() && !isAsioMode()) {
        // Device lost — show desired device name with "(Disconnected)"
        auto desired = engine_.getDesiredOutputDevice();
        if (desired.isNotEmpty() && !outputs.contains(desired)) {
            int dcId = outputs.size() + outputIdOffset;
            outputCombo_.addItem(desired + " (Disconnected)", dcId);
            outputCombo_.setSelectedId(dcId, juce::dontSendNotification);
        } else {
            outputCombo_.setSelectedId(1, juce::dontSendNotification); // "None" fallback
        }
    } else {
        juce::String outputName = setup.outputDeviceName;
        if (outputName.isEmpty()) {
            if (auto* device = engine_.getDeviceManager().getCurrentAudioDevice())
                outputName = device->getName();
        }
        int outIdx = outputs.indexOf(outputName);
        if (outIdx >= 0)
            outputCombo_.setSelectedId(outIdx + outputIdOffset, juce::dontSendNotification);
        else if (outputs.size() > 0)
            outputCombo_.setSelectedId(outputIdOffset, juce::dontSendNotification);
    }

    // Rebuild ASIO channel lists if applicable
    if (isAsioMode())
        rebuildChannelLists();
}

void AudioSettings::rebuildChannelLists()
{
    // Input channels (stereo pairs)
    inputChCombo_.clear(juce::dontSendNotification);
    auto inNames = engine_.getInputChannelNames();
    int inPairs = inNames.size() / 2;
    if (inNames.size() % 2 != 0) inPairs++;  // odd channel count

    for (int i = 0; i < inPairs; ++i) {
        int ch1 = i * 2;
        int ch2 = ch1 + 1;
        juce::String label;
        if (ch2 < inNames.size())
            label = inNames[ch1] + " + " + inNames[ch2];
        else
            label = inNames[ch1];
        inputChCombo_.addItem(label, i + 1);
    }
    // Also add individual channels for mono use
    if (inNames.size() == 0) {
        inputChCombo_.addItem("1-2", 1);
    }

    // Select current input channel offset
    int inOffset = engine_.getActiveInputChannelOffset();
    if (inOffset >= 0)
        inputChCombo_.setSelectedId((inOffset / 2) + 1, juce::dontSendNotification);
    else if (inputChCombo_.getNumItems() > 0)
        inputChCombo_.setSelectedId(1, juce::dontSendNotification);

    // Output channels (stereo pairs)
    outputChCombo_.clear(juce::dontSendNotification);
    auto outNames = engine_.getOutputChannelNames();
    int outPairs = outNames.size() / 2;
    if (outNames.size() % 2 != 0) outPairs++;

    for (int i = 0; i < outPairs; ++i) {
        int ch1 = i * 2;
        int ch2 = ch1 + 1;
        juce::String label;
        if (ch2 < outNames.size())
            label = outNames[ch1] + " + " + outNames[ch2];
        else
            label = outNames[ch1];
        outputChCombo_.addItem(label, i + 1);
    }
    if (outNames.size() == 0) {
        outputChCombo_.addItem("1-2", 1);
    }

    // Select current output channel offset
    int outOffset = engine_.getActiveOutputChannelOffset();
    if (outOffset >= 0)
        outputChCombo_.setSelectedId((outOffset / 2) + 1, juce::dontSendNotification);
    else if (outputChCombo_.getNumItems() > 0)
        outputChCombo_.setSelectedId(1, juce::dontSendNotification);
}

void AudioSettings::rebuildSampleRateList()
{
    sampleRateCombo_.clear(juce::dontSendNotification);
    auto rates = engine_.getAvailableSampleRates();
    auto& monitor = engine_.getLatencyMonitor();
    double currentSR = monitor.getSampleRate();

    for (int i = 0; i < rates.size(); ++i) {
        sampleRateCombo_.addItem(juce::String(static_cast<int>(rates[i])) + " Hz", i + 1);
        if (std::abs(rates[i] - currentSR) < 1.0)
            sampleRateCombo_.setSelectedId(i + 1, juce::dontSendNotification);
    }
}

void AudioSettings::rebuildBufferSizeList()
{
    bufferSizeCombo_.clear(juce::dontSendNotification);
    auto sizes = engine_.getAvailableBufferSizes();
    int currentBS = engine_.getLatencyMonitor().getBufferSize();

    for (int i = 0; i < sizes.size(); ++i) {
        bufferSizeCombo_.addItem(juce::String(sizes[i]) + " samples", i + 1);
        if (sizes[i] == currentBS)
            bufferSizeCombo_.setSelectedId(i + 1, juce::dontSendNotification);
    }
}

void AudioSettings::updateLatencyDisplay()
{
    auto& monitor = engine_.getLatencyMonitor();
    double sr = monitor.getSampleRate();
    int bs = monitor.getBufferSize();

    if (sr > 0.0) {
        double latencyMs = (static_cast<double>(bs) / sr) * 1000.0 * 2.0;
        latencyValueLabel_.setText(
            juce::String(latencyMs, 2) + " ms  (" + juce::String(bs) + " samples @ "
                + juce::String(static_cast<int>(sr)) + " Hz)",
            juce::dontSendNotification);
    } else {
        latencyValueLabel_.setText("-- ms", juce::dontSendNotification);
    }
}

void AudioSettings::updateChannelModeDescription()
{
    if (stereoButton_.getToggleState()) {
        channelModeDescLabel_.setText(
            "Stereo: Input channels pass through as-is (L/R)",
            juce::dontSendNotification);
    } else {
        channelModeDescLabel_.setText(
            "Mono: Mix L+R to mono, output to both channels",
            juce::dontSendNotification);
    }
}

void AudioSettings::setOutputNone(bool none)
{
    engine_.setOutputNone(none);
    if (!isAsioMode()) {
        if (none)
            outputCombo_.setSelectedId(1, juce::dontSendNotification); // "None"
        else
            rebuildDeviceLists();
    }
}

} // namespace directpipe
