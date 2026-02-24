/**
 * @file OutputPanel.cpp
 * @brief Monitor output control panel implementation
 */

#include "OutputPanel.h"

namespace directpipe {

// ─── Construction / Destruction ─────────────────────────────────────────────

OutputPanel::OutputPanel(AudioEngine& engine)
    : engine_(engine)
{
    // ── Section title ──
    titleLabel_.setFont(juce::Font(16.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(titleLabel_);

    // ── Monitor section ──

    // Device selector
    monitorDeviceLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(monitorDeviceLabel_);

    monitorDeviceCombo_.onChange = [this] { onMonitorDeviceSelected(); };
    addAndMakeVisible(monitorDeviceCombo_);

    // Volume slider
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

    // Enable button
    monitorEnableButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    monitorEnableButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    monitorEnableButton_.onClick = [this] { onMonitorEnableToggled(); };
    addAndMakeVisible(monitorEnableButton_);

    // ── Initial state ──
    refreshMonitorDeviceList();

    // Sync volume with output router
    auto& router = engine_.getOutputRouter();
    monitorVolumeSlider_.setValue(
        static_cast<double>(router.getVolume(OutputRouter::Output::Monitor)) * 100.0,
        juce::dontSendNotification);

    // Sync enable state
    monitorEnableButton_.setToggleState(
        router.isEnabled(OutputRouter::Output::Monitor),
        juce::dontSendNotification);

    // Start periodic status polling at 4 Hz
    startTimerHz(4);
}

OutputPanel::~OutputPanel()
{
    stopTimer();
}

// ─── Paint ──────────────────────────────────────────────────────────────────

void OutputPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));

    auto area = getLocalBounds().reduced(4);
    g.setColour(juce::Colour(kSurfaceColour));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);
}

// ─── Layout ─────────────────────────────────────────────────────────────────

void OutputPanel::resized()
{
    auto bounds = getLocalBounds().reduced(12);
    constexpr int rowH = 28;
    constexpr int gap  = 8;
    constexpr int labelW = 80;

    int y = bounds.getY();

    // Panel title
    titleLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    // Monitor device selector
    monitorDeviceLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    monitorDeviceCombo_.setBounds(bounds.getX() + labelW + gap, y,
                                  bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Monitor volume
    monitorVolumeLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    monitorVolumeSlider_.setBounds(bounds.getX() + labelW + gap, y,
                                   bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Monitor enable
    monitorEnableButton_.setBounds(bounds.getX() + labelW + gap, y, 120, rowH);
}

// ─── Timer callback ─────────────────────────────────────────────────────────

void OutputPanel::timerCallback()
{
    // Sync enable state with engine (reflects Panic Mute and external control)
    auto& router = engine_.getOutputRouter();

    bool monEnabled = router.isEnabled(OutputRouter::Output::Monitor);
    if (monitorEnableButton_.getToggleState() != monEnabled) {
        monitorEnableButton_.setToggleState(monEnabled, juce::dontSendNotification);
    }
}

// ─── Monitor device list ────────────────────────────────────────────────────

void OutputPanel::refreshMonitorDeviceList()
{
    monitorDeviceCombo_.clear(juce::dontSendNotification);

    // Always show Windows Audio (WASAPI) output devices for monitoring
    auto devices = engine_.getWasapiOutputDevices();
    for (int i = 0; i < devices.size(); ++i) {
        monitorDeviceCombo_.addItem(devices[i], i + 1);
    }

    // Try to select the currently active output device
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    engine_.getDeviceManager().getAudioDeviceSetup(setup);

    int idx = devices.indexOf(setup.outputDeviceName);
    if (idx >= 0) {
        monitorDeviceCombo_.setSelectedId(idx + 1, juce::dontSendNotification);
    } else if (auto* device = engine_.getDeviceManager().getCurrentAudioDevice()) {
        int nameIdx = devices.indexOf(device->getName());
        if (nameIdx >= 0)
            monitorDeviceCombo_.setSelectedId(nameIdx + 1, juce::dontSendNotification);
    }
}

// ─── Callbacks ──────────────────────────────────────────────────────────────

void OutputPanel::onMonitorDeviceSelected()
{
    auto selectedText = monitorDeviceCombo_.getText();
    if (selectedText.isNotEmpty()) {
        engine_.setOutputDevice(selectedText);
        if (onSettingsChanged) onSettingsChanged();
    }
}

void OutputPanel::onMonitorVolumeChanged()
{
    float volume = static_cast<float>(monitorVolumeSlider_.getValue()) / 100.0f;
    engine_.getOutputRouter().setVolume(OutputRouter::Output::Monitor, volume);
    if (onSettingsChanged) onSettingsChanged();
}

void OutputPanel::onMonitorEnableToggled()
{
    bool enabled = monitorEnableButton_.getToggleState();
    engine_.getOutputRouter().setEnabled(OutputRouter::Output::Monitor, enabled);
    engine_.setMonitorEnabled(enabled);
    if (onSettingsChanged) onSettingsChanged();
}

} // namespace directpipe
