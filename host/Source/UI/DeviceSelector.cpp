/**
 * @file DeviceSelector.cpp
 * @brief Audio device selector implementation
 */

#include "DeviceSelector.h"

namespace directpipe {

DeviceSelector::DeviceSelector(AudioEngine& engine)
    : engine_(engine)
{
    // Device combo
    addAndMakeVisible(deviceCombo_);
    addAndMakeVisible(deviceLabel_);
    deviceCombo_.onChange = [this] { onDeviceSelected(); };

    // Sample rate combo
    addAndMakeVisible(sampleRateCombo_);
    addAndMakeVisible(sampleRateLabel_);
    sampleRateCombo_.addItem("44100 Hz", 1);
    sampleRateCombo_.addItem("48000 Hz", 2);
    sampleRateCombo_.addItem("96000 Hz", 3);
    sampleRateCombo_.setSelectedId(2);  // Default 48kHz
    sampleRateCombo_.onChange = [this] { onSampleRateChanged(); };

    // Buffer size combo
    addAndMakeVisible(bufferSizeCombo_);
    addAndMakeVisible(bufferSizeLabel_);
    bufferSizeCombo_.addItem("64 samples", 1);
    bufferSizeCombo_.addItem("128 samples", 2);
    bufferSizeCombo_.addItem("256 samples", 3);
    bufferSizeCombo_.addItem("512 samples", 4);
    bufferSizeCombo_.setSelectedId(2);  // Default 128
    bufferSizeCombo_.onChange = [this] { onBufferSizeChanged(); };

    // Listen for device changes
    engine_.getDeviceManager().addChangeListener(this);

    refreshDeviceList();
}

DeviceSelector::~DeviceSelector()
{
    engine_.getDeviceManager().removeChangeListener(this);
}

void DeviceSelector::paint(juce::Graphics& /*g*/)
{
    // Background handled by parent
}

void DeviceSelector::resized()
{
    auto bounds = getLocalBounds();
    int labelWidth = 100;
    int comboHeight = 24;
    int gap = 6;
    int y = 0;

    // Row 1: Device selection
    deviceLabel_.setBounds(0, y, labelWidth, comboHeight);
    deviceCombo_.setBounds(labelWidth + gap, y, bounds.getWidth() - labelWidth - gap, comboHeight);
    y += comboHeight + gap;

    // Row 2: Sample rate and buffer size side by side
    int halfWidth = (bounds.getWidth() - labelWidth - gap) / 2;
    sampleRateLabel_.setBounds(0, y, labelWidth, comboHeight);
    sampleRateCombo_.setBounds(labelWidth + gap, y, halfWidth - gap / 2, comboHeight);
    bufferSizeLabel_.setBounds(labelWidth + gap + halfWidth, y, 80, comboHeight);
    bufferSizeCombo_.setBounds(labelWidth + gap + halfWidth + 80 + gap, y,
                                halfWidth - 80 - gap, comboHeight);
}

void DeviceSelector::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    refreshDeviceList();
}

void DeviceSelector::refreshDeviceList()
{
    deviceCombo_.clear(juce::dontSendNotification);

    auto devices = engine_.getAvailableInputDevices();
    for (int i = 0; i < devices.size(); ++i) {
        deviceCombo_.addItem(devices[i], i + 1);
    }

    // Select current device
    if (auto* device = engine_.getDeviceManager().getCurrentAudioDevice()) {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        engine_.getDeviceManager().getAudioDeviceSetup(setup);

        int idx = devices.indexOf(setup.inputDeviceName);
        if (idx >= 0) {
            deviceCombo_.setSelectedId(idx + 1, juce::dontSendNotification);
        }
    }
}

void DeviceSelector::onDeviceSelected()
{
    auto selectedText = deviceCombo_.getText();
    if (selectedText.isNotEmpty()) {
        engine_.setInputDevice(selectedText);
    }
}

void DeviceSelector::onSampleRateChanged()
{
    int id = sampleRateCombo_.getSelectedId();
    double rates[] = {44100.0, 48000.0, 96000.0};
    if (id >= 1 && id <= 3) {
        engine_.setSampleRate(rates[id - 1]);
    }
}

void DeviceSelector::onBufferSizeChanged()
{
    int id = bufferSizeCombo_.getSelectedId();
    int sizes[] = {64, 128, 256, 512};
    if (id >= 1 && id <= 4) {
        engine_.setBufferSize(sizes[id - 1]);
    }
}

} // namespace directpipe
