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
#include "AudioSettingsPolicy.h"
#include "../Platform/WindowsMicLevelKeeper.h"

namespace directpipe {

// ─── Construction / Destruction ─────────────────────────────────────────────

AudioSettings::AudioSettings(AudioEngine& engine,
                             WindowsMicLevelKeeper* micLevelKeeper)
    : engine_(engine), micLevelKeeper_(micLevelKeeper)
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

    windowsMicLevelButton_.setComponentID("windowsMicLevel100");
    windowsMicLevelButton_.setName("Keep Windows mic level at 100");
    windowsMicLevelButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    windowsMicLevelButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    windowsMicLevelButton_.setTooltip(
        "Keeps the selected microphone's Windows input level at 100 for every app. "
        "Manual Windows slider changes are corrected while enabled. "
        "100 is the driver maximum, not guaranteed 0 dB. Mute and boost are unchanged.");
    windowsMicLevelButton_.onClick = [this] { onWindowsMicLevelChanged(); };
    addAndMakeVisible(windowsMicLevelButton_);
    windowsMicLevelStatusLabel_.setComponentID("windowsMicLevelStatus");
    windowsMicLevelStatusLabel_.setFont(juce::Font(11.0f));
    windowsMicLevelStatusLabel_.setMinimumHorizontalScale(1.0f);
    windowsMicLevelStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(windowsMicLevelStatusLabel_);

    // ── Output device ──
    styleLabel(outputLabel_);
    styleCombo(outputCombo_);
    outputCombo_.onChange = [this] { onOutputDeviceChanged(); };

    // Capture selects hardware inputs; legacy Channel Mode still controls
    // pre-effects fold-down and keeps its saved value when capture changes.
    styleLabel(inputCaptureLabel_);
    styleCombo(inputCaptureCombo_);
    inputCaptureCombo_.setComponentID("inputCaptureMode");
    inputCaptureCombo_.setName("Input capture");
    inputCaptureCombo_.addItem("Stereo pair (existing)", 1);
    inputCaptureCombo_.addItem("Mono (single channel)", 2);
    inputCaptureCombo_.setTooltip(
        "Stereo pair keeps the existing input routing. Mono receives one selected "
        "hardware input and copies it to L/R before effects. Channel Mode remains separate.");
    inputCaptureCombo_.onChange = [this] { onInputCaptureModeChanged(); };

    // ── Hardware channel selection ──
    styleLabel(inputChLabel_);
    styleCombo(inputChCombo_);
    inputChCombo_.setComponentID("inputChannel");
    inputChCombo_.setName("Input channel");
    inputChCombo_.setTextWhenNothingSelected("No input channels available");
    inputChCombo_.onChange = [this] { onInputChannelChanged(); };

    styleLabel(outputChLabel_);
    styleCombo(outputChCombo_);
    outputChCombo_.setComponentID("outputChannel");
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
    if (micLevelKeeper_)
        micLevelKeeper_->addChangeListener(this);

    // Refresh device list when combos are clicked (scan before popup opens)
    inputCombo_.addMouseListener(this, true);
    outputCombo_.addMouseListener(this, true);

    // Synchronise the UI with current engine state
    refreshFromEngine();
}

AudioSettings::~AudioSettings()
{
    if (micLevelKeeper_)
        micLevelKeeper_->removeChangeListener(this);
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

    windowsMicLevelButton_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH;
    windowsMicLevelStatusLabel_.setBounds(bounds.getX() + 4, y,
                                         bounds.getWidth() - 4, 30);
    y += 30 + gap;

    // Output device (always visible)
    outputLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    outputCombo_.setBounds(bounds.getX() + labelW + gap, y,
                           bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Capture mode stays visible even while the device is disconnected.
    inputCaptureLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    inputCaptureCombo_.setBounds(bounds.getX() + labelW + gap, y,
                                 bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Non-ASIO input selection is exposed only after opting into mono capture.
    bool asio = isAsioMode();
    const bool showInputChannels = asio || engine_.isMonoInputEnabled();
    inputChLabel_.setVisible(showInputChannels);
    inputChCombo_.setVisible(showInputChannels);
    if (showInputChannels) {
        inputChLabel_.setBounds(bounds.getX(), y, labelW, rowH);
        inputChCombo_.setBounds(bounds.getX() + labelW + gap, y,
                                bounds.getWidth() - labelW - gap, rowH);
        y += rowH + gap;
    }

    // Output routing retains the existing ASIO-only pair selector.
    outputChLabel_.setVisible(asio);
    outputChCombo_.setVisible(asio);
    if (asio) {
        outputChLabel_.setBounds(bounds.getX(), y, labelW, rowH);
        outputChCombo_.setBounds(bounds.getX() + labelW + gap, y,
                                 bounds.getWidth() - labelW - gap, rowH);
        y += rowH + gap;
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
    updateWindowsMicInputContext();
    refreshWindowsMicLevel();

    // Update ASIO-specific visibility
    bool asio = isAsioMode();
    asioControlBtn_.setVisible(asio);
    outputChLabel_.setVisible(asio);
    outputChCombo_.setVisible(asio);
    resized();
}

// ─── ChangeListener ─────────────────────────────────────────────────────────

void AudioSettings::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == micLevelKeeper_) {
        refreshWindowsMicLevel();
        return;
    }
    // Device manager changed — rebuild all lists (handles device plug/unplug)
    rebuildDeviceLists();
    rebuildSampleRateList();
    rebuildBufferSizeList();
    updateChannelModeDescription();
    updateLatencyDisplay();
    updateWindowsMicInputContext();
    refreshWindowsMicLevel();
    resized();

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
        suspendWindowsMicInput();
        auto result = engine_.setAudioDeviceType(types[id - 1]);

        // If switch failed, sync combo to actual current driver type
        if (!result) {
            auto actualType = engine_.getCurrentDeviceType();
            int actualIdx = types.indexOf(actualType);
            if (actualIdx >= 0)
                driverCombo_.setSelectedId(actualIdx + 1, juce::dontSendNotification);
            if (onError) onError(result.message);
            refreshFromEngine();
            return;
        }

        rebuildDeviceLists();
        rebuildSampleRateList();
        rebuildBufferSizeList();
        updateLatencyDisplay();

        bool asio = isAsioMode();
        asioControlBtn_.setVisible(asio);
        outputChLabel_.setVisible(asio);
        outputChCombo_.setVisible(asio);
        updateWindowsMicInputContext();
        refreshWindowsMicLevel();
        resized();

        if (onSettingsChanged) onSettingsChanged();
    }
}

void AudioSettings::onInputDeviceChanged()
{
    auto selectedText = inputCombo_.getText();
    if (selectedText.isEmpty()
        || audio_settings_detail::isRecoveryPlaceholderSelection(
            inputCombo_.getSelectedId(), inputRecoveryPlaceholderId_))
        return;

    suspendWindowsMicInput();
    if (isAsioMode()) {
        auto r = engine_.setAsioDevice(selectedText);
        if (!r) {
            if (onError) onError(r.message);
            refreshFromEngine();
            return;
        }

        // Sync output combo to match (ASIO single device)
        auto outputs = engine_.getAvailableOutputDevices();
        int outIdx = outputs.indexOf(selectedText);
        if (outIdx >= 0)
            outputCombo_.setSelectedId(outIdx + 1, juce::dontSendNotification);

        rebuildChannelLists();
    } else {
        auto r = engine_.setInputDevice(selectedText);
        if (!r) {
            if (onError) onError(r.message);
            refreshFromEngine();
            return;
        }
    }

    refreshFromEngine();

    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onOutputDeviceChanged()
{
    auto selectedText = outputCombo_.getText();
    if (selectedText.isEmpty()
        || audio_settings_detail::isRecoveryPlaceholderSelection(
            outputCombo_.getSelectedId(), outputRecoveryPlaceholderId_))
        return;

    // "None" = mute main output (keep device open for input)
    if (selectedText == "None") {
        engine_.setOutputNone(true);
        if (onSettingsChanged) onSettingsChanged();
        return;
    }

    if (isAsioMode()) {
        suspendWindowsMicInput();
        auto r = engine_.setAsioDevice(selectedText);
        if (!r) {
            if (onError) onError(r.message);
            refreshFromEngine();
            return;
        }

        // Sync input combo to match (ASIO single device)
        auto inputs = engine_.getAvailableInputDevices();
        int inIdx = inputs.indexOf(selectedText);
        if (inIdx >= 0)
            inputCombo_.setSelectedId(inIdx + 1, juce::dontSendNotification);

        rebuildChannelLists();
    } else {
        auto r = engine_.setOutputDevice(selectedText);
        if (!r) {
            if (onError) onError(r.message);
            refreshFromEngine();
            return;
        }
    }

    if (engine_.isOutputNone())
        engine_.setOutputNone(false);

    refreshFromEngine();

    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onInputCaptureModeChanged()
{
    const int selectedMode = inputCaptureCombo_.getSelectedId();
    if (selectedMode != 1 && selectedMode != 2)
        return;

    const auto result = engine_.setInputCaptureMode(
        selectedMode == 2, engine_.getActiveInputChannelOffset());
    if (!result) {
        if (onError) onError(result.message);
        refreshFromEngine();
        return;
    }

    refreshFromEngine();
    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onInputChannelChanged()
{
    int id = inputChCombo_.getSelectedId();
    if (id < 1) return;

    const bool monoInput = engine_.isMonoInputEnabled();
    int firstChannel = (id - 1) * (monoInput ? 1 : 2);
    int totalChannels = engine_.getInputChannelNames().size();
    int numCh = monoInput
        ? (firstChannel < totalChannels ? 1 : 0)
        : audio_settings_detail::channelCountForSelectedPair(
            firstChannel, totalChannels);
    if (numCh <= 0)
        return;
    auto r = engine_.setInputCaptureMode(monoInput, firstChannel);
    if (!r) {
        if (onError) onError(r.message);
        refreshFromEngine();
        return;
    }

    refreshFromEngine();
    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onOutputChannelChanged()
{
    int id = outputChCombo_.getSelectedId();
    if (id < 1) return;

    int firstChannel = (id - 1) * 2;
    int totalChannels = engine_.getOutputChannelNames().size();
    int numCh = audio_settings_detail::channelCountForSelectedPair(
        firstChannel, totalChannels);
    if (numCh <= 0)
        return;
    auto r = engine_.setActiveOutputChannels(firstChannel, numCh);
    if (!r) {
        if (onError) onError(r.message);
        rebuildChannelLists();
        return;
    }

    if (onSettingsChanged) onSettingsChanged();
}

void AudioSettings::onSampleRateChanged()
{
    int id = sampleRateCombo_.getSelectedId();
    if (id < 1) return;

    auto rates = engine_.getAvailableSampleRates();
    if (id - 1 < rates.size()) {
        auto r = engine_.setSampleRate(rates[id - 1]);
        if (!r) {
            if (onError) onError(r.message);
            return;
        }
        rebuildSampleRateList();
        rebuildBufferSizeList();
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
        if (!r) {
            if (onError) onError(r.message);
            return;
        }
        rebuildSampleRateList();
        rebuildBufferSizeList();
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

void AudioSettings::updateWindowsMicInputContext()
{
    if (!micLevelKeeper_)
        return;
    const auto setup = engine_.getDeviceManager().getAudioDeviceSetup();
    auto* device = engine_.getDeviceManager().getCurrentAudioDevice();
    const bool active = device && device->isPlaying()
        && !engine_.isInputDeviceLost()
        && !device->getActiveInputChannels().isZero();
    micLevelKeeper_->updateInputContext({ engine_.getCurrentDeviceType(),
                                         setup.inputDeviceName, active,
                                         active ? device->getWindowsInputEndpointId() : juce::String{} });
}

void AudioSettings::suspendWindowsMicInput()
{
    if (micLevelKeeper_)
        micLevelKeeper_->updateInputContext({});
}

void AudioSettings::refreshWindowsMicLevel()
{
    const auto status = micLevelKeeper_ ? micLevelKeeper_->getStatus()
        : WindowsMicLevelKeeper::Status{};
    windowsMicLevelButton_.setToggleState(status.enabled, juce::dontSendNotification);
    windowsMicLevelButton_.setEnabled(micLevelKeeper_ && (status.canEnable || status.enabled));
    const auto detail = micLevelKeeper_ ? status.message
        : juce::String("Windows microphone level control is unavailable.");
    const auto message = status.canEnable || status.enabled
        ? "Applies to this mic in all apps. " + detail : detail;
    windowsMicLevelStatusLabel_.setText(message.substring(0, 240), juce::dontSendNotification);
    windowsMicLevelStatusLabel_.setTooltip(message);
}

void AudioSettings::onWindowsMicLevelChanged()
{
    if (!micLevelKeeper_) {
        refreshWindowsMicLevel();
        return;
    }
    juce::String error;
    if (!micLevelKeeper_->setEnabledForCurrentEndpoint(
            windowsMicLevelButton_.getToggleState(), error)) {
        refreshWindowsMicLevel();
        if (onError && error.isNotEmpty())
            onError(error);
        return;
    }
    refreshWindowsMicLevel();
    if (onSettingsChanged)
        onSettingsChanged();
}

bool AudioSettings::isAsioMode() const
{
    return engine_.getCurrentDeviceType().containsIgnoreCase("ASIO");
}

void AudioSettings::rebuildDeviceLists()
{
    inputRecoveryPlaceholderId_ = 0;
    outputRecoveryPlaceholderId_ = 0;

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

    const bool asio = isAsioMode();
    const bool inputLost = engine_.isInputDeviceLost();
    const bool outputLost = engine_.isOutputAutoMuted();
    bool inputRecoverySelected = false;
    if (audio_settings_detail::needsRecoveryPlaceholder(
            audio_settings_detail::DeviceDirection::Input,
            asio, inputLost, outputLost)) {
        auto desired = engine_.getDesiredInputDevice();
        if (desired.isEmpty() && asio)
            desired = engine_.getDesiredOutputDevice();
        const auto recovery = audio_settings_detail::makeRecoverySelection(
            desired, inputs, 1);
        if (recovery.placeholderId > 0) {
            inputRecoveryPlaceholderId_ = recovery.placeholderId;
            inputCombo_.addItem(
                recovery.placeholderText, recovery.placeholderId);
            inputCombo_.setItemEnabled(recovery.placeholderId, false);
            inputCombo_.setSelectedId(
                recovery.placeholderId, juce::dontSendNotification);
            inputRecoverySelected = true;
        }
    }
    if (!inputRecoverySelected) {
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
    if (engine_.isOutputNone() && !asio) {
        outputCombo_.setSelectedId(1, juce::dontSendNotification); // "None" (intentional)
    } else if (audio_settings_detail::needsRecoveryPlaceholder(
                   audio_settings_detail::DeviceDirection::Output,
                   asio, inputLost, outputLost)) {
        auto desired = engine_.getDesiredOutputDevice();
        if (desired.isEmpty() && asio)
            desired = engine_.getDesiredInputDevice();
        const auto recovery = audio_settings_detail::makeRecoverySelection(
            desired, outputs, outputIdOffset);
        if (recovery.placeholderId > 0) {
            outputRecoveryPlaceholderId_ = recovery.placeholderId;
            outputCombo_.addItem(
                recovery.placeholderText, recovery.placeholderId);
            outputCombo_.setItemEnabled(recovery.placeholderId, false);
            outputCombo_.setSelectedId(
                recovery.placeholderId, juce::dontSendNotification);
        } else if (outputs.size() > 0) {
            outputCombo_.setSelectedId(
                outputIdOffset, juce::dontSendNotification);
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

    rebuildChannelLists();
}

void AudioSettings::rebuildChannelLists()
{
    const bool monoInput = engine_.isMonoInputEnabled();
    inputCaptureCombo_.setSelectedId(monoInput ? 2 : 1, juce::dontSendNotification);

    inputChCombo_.clear(juce::dontSendNotification);
    auto inNames = engine_.getInputChannelNames();
    const bool recoveringInput = audio_settings_detail::needsRecoveryPlaceholder(
        audio_settings_detail::DeviceDirection::Input, isAsioMode(),
        engine_.isInputDeviceLost(), engine_.isOutputAutoMuted());
    const bool canSelectInput = !inNames.isEmpty() && !recoveringInput;
    inputCaptureCombo_.setEnabled(canSelectInput);
    inputChCombo_.setEnabled(canSelectInput);

    if (monoInput) {
        // Combo IDs map directly to physical index + 1, including an odd tail.
        for (int channel = 0; channel < inNames.size(); ++channel)
            inputChCombo_.addItem(inNames[channel], channel + 1);
    } else {
        const int inPairs = inNames.size() == 1 ? 1 : inNames.size() / 2;
        for (int pair = 0; pair < inPairs; ++pair) {
            const int first = pair * 2;
            const auto label = first + 1 < inNames.size()
                ? inNames[first] + " + " + inNames[first + 1]
                : inNames[first];
            inputChCombo_.addItem(label, pair + 1);
        }
    }
    if (inNames.isEmpty() && !monoInput)
        inputChCombo_.addItem("1-2", 1);

    // Select current input channel offset
    int inOffset = engine_.getActiveInputChannelOffset();
    const int desiredInputId = inOffset >= 0
        ? inOffset / (monoInput ? 1 : 2) + 1 : 0;
    if (desiredInputId > 0 && desiredInputId <= inputChCombo_.getNumItems())
        inputChCombo_.setSelectedId(desiredInputId, juce::dontSendNotification);
    else if (inputChCombo_.getNumItems() > 0)
        inputChCombo_.setSelectedId(1, juce::dontSendNotification);
    inputChCombo_.setTooltip(monoInput
        ? "One hardware input copied to L/R before effects: " + inputChCombo_.getText()
        : "Input pair before Channel Mode processing: " + inputChCombo_.getText());

    // Output channels (stereo pairs)
    outputChCombo_.clear(juce::dontSendNotification);
    auto outNames = engine_.getOutputChannelNames();
    const int outPairs = outNames.size() == 1 ? 1 : outNames.size() / 2;

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
    const int desiredOutputId = outOffset >= 0 ? (outOffset / 2) + 1 : 0;
    if (desiredOutputId > 0 && desiredOutputId <= outputChCombo_.getNumItems())
        outputChCombo_.setSelectedId(desiredOutputId, juce::dontSendNotification);
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
    const int totalLatencySamples =
        monitor.getInputLatencySamples() + monitor.getOutputLatencySamples();

    if (sr > 0.0) {
        const double latencyMs =
            (static_cast<double>(totalLatencySamples) / sr) * 1000.0;
        latencyValueLabel_.setText(
            juce::String(latencyMs, 2) + " ms  (" + juce::String(totalLatencySamples) + " samples @ "
                + juce::String(static_cast<int>(sr)) + " Hz)",
            juce::dontSendNotification);
    } else {
        latencyValueLabel_.setText("-- ms", juce::dontSendNotification);
    }
}

void AudioSettings::updateChannelModeDescription()
{
    if (engine_.isMonoInputEnabled()) {
        channelModeDescLabel_.setText(
            "One input copied to L/R before effects",
            juce::dontSendNotification);
    } else if (stereoButton_.getToggleState()) {
        channelModeDescLabel_.setText(
            "Input pair passes to effects as L/R",
            juce::dontSendNotification);
    } else {
        channelModeDescLabel_.setText(
            "Input pair mixed to mono before effects",
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
