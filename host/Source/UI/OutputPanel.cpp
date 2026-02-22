/**
 * @file OutputPanel.cpp
 * @brief Output status and control panel implementation
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

    // ════════════════════════════════════════════════════════════════════════
    // Virtual Loop Mic section
    // ════════════════════════════════════════════════════════════════════════

    vmicSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    vmicSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(vmicSectionLabel_);

    // Status indicator (Disconnected by default)
    vmicStatusLabel_.setFont(juce::Font(12.0f));
    vmicStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kRedColour));
    addAndMakeVisible(vmicStatusLabel_);

    // Volume slider
    vmicVolumeLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(vmicVolumeLabel_);

    vmicVolumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    vmicVolumeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    vmicVolumeSlider_.setRange(0.0, 100.0, 1.0);
    vmicVolumeSlider_.setValue(100.0, juce::dontSendNotification);
    vmicVolumeSlider_.setTextValueSuffix(" %");
    vmicVolumeSlider_.setColour(juce::Slider::thumbColourId, juce::Colour(kAccentColour));
    vmicVolumeSlider_.setColour(juce::Slider::trackColourId, juce::Colour(kAccentColour).withAlpha(0.4f));
    vmicVolumeSlider_.setColour(juce::Slider::backgroundColourId, juce::Colour(kSurfaceColour).brighter(0.1f));
    vmicVolumeSlider_.setColour(juce::Slider::textBoxTextColourId, juce::Colour(kTextColour));
    vmicVolumeSlider_.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    vmicVolumeSlider_.onValueChange = [this] { onVirtualMicVolumeChanged(); };
    addAndMakeVisible(vmicVolumeSlider_);

    // Mute button
    vmicMuteButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    vmicMuteButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kRedColour));
    vmicMuteButton_.onClick = [this] { onVirtualMicMuteToggled(); };
    addAndMakeVisible(vmicMuteButton_);

    // ════════════════════════════════════════════════════════════════════════
    // Monitor section
    // ════════════════════════════════════════════════════════════════════════

    monitorSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    monitorSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(monitorSectionLabel_);

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

    // Mute button
    monitorMuteButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    monitorMuteButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kRedColour));
    monitorMuteButton_.onClick = [this] { onMonitorMuteToggled(); };
    addAndMakeVisible(monitorMuteButton_);

    // ── Initial state ──
    refreshMonitorDeviceList();

    // Sync volume sliders with output router
    auto& router = engine_.getOutputRouter();
    vmicVolumeSlider_.setValue(
        static_cast<double>(router.getVolume(OutputRouter::Output::VirtualMic)) * 100.0,
        juce::dontSendNotification);
    monitorVolumeSlider_.setValue(
        static_cast<double>(router.getVolume(OutputRouter::Output::Monitor)) * 100.0,
        juce::dontSendNotification);

    // Sync mute states
    vmicMuteButton_.setToggleState(
        !router.isEnabled(OutputRouter::Output::VirtualMic),
        juce::dontSendNotification);
    monitorMuteButton_.setToggleState(
        !router.isEnabled(OutputRouter::Output::Monitor),
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

    // Draw a rounded-rect surface behind the controls
    auto area = getLocalBounds().reduced(4);
    g.setColour(juce::Colour(kSurfaceColour));
    g.fillRoundedRectangle(area.toFloat(), 6.0f);

    // Draw a thin separator between Virtual Mic and Monitor sections
    auto bounds = getLocalBounds().reduced(12);
    constexpr int rowH = 28;
    constexpr int gap  = 8;

    // Calculate Y position of the separator (after Virtual Mic section)
    // Title + vmicSection + status + volume + mute = 5 rows
    int separatorY = bounds.getY() + (rowH + gap) * 5 - gap / 2;
    g.setColour(juce::Colour(kDimTextColour).withAlpha(0.3f));
    g.drawHorizontalLine(separatorY, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
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

    // ── Virtual Loop Mic section ──
    vmicSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth() / 2, rowH);
    vmicStatusLabel_.setBounds(bounds.getX() + bounds.getWidth() / 2, y,
                               bounds.getWidth() / 2, rowH);
    y += rowH + gap;

    // Virtual Mic volume
    vmicVolumeLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    vmicVolumeSlider_.setBounds(bounds.getX() + labelW + gap, y,
                                bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Virtual Mic mute
    vmicMuteButton_.setBounds(bounds.getX() + labelW + gap, y, 120, rowH);
    y += rowH + gap + 8; // extra gap for separator

    // ── Monitor section ──
    monitorSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
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

    // Monitor mute
    monitorMuteButton_.setBounds(bounds.getX() + labelW + gap, y, 120, rowH);
}

// ─── Timer callback ─────────────────────────────────────────────────────────

void OutputPanel::timerCallback()
{
    updateVirtualMicStatus();
}

// ─── Virtual Mic status ─────────────────────────────────────────────────────

void OutputPanel::updateVirtualMicStatus()
{
    // Query the output router for shared-memory / OBS connection status.
    // The Virtual Loop Mic is considered "connected" when the shared memory
    // IPC is active (meaning OBS or another consumer is reading from it).
    auto& router = engine_.getOutputRouter();
    bool connected = router.isOBSConnected();

    if (connected != vmicConnected_) {
        vmicConnected_ = connected;

        if (vmicConnected_) {
            vmicStatusLabel_.setText("Connected", juce::dontSendNotification);
            vmicStatusLabel_.setColour(juce::Label::textColourId,
                                       juce::Colour(kGreenColour));
        } else {
            vmicStatusLabel_.setText("Disconnected", juce::dontSendNotification);
            vmicStatusLabel_.setColour(juce::Label::textColourId,
                                       juce::Colour(kRedColour));
        }
    }
}

// ─── Monitor device list ────────────────────────────────────────────────────

void OutputPanel::refreshMonitorDeviceList()
{
    monitorDeviceCombo_.clear(juce::dontSendNotification);

    auto devices = engine_.getAvailableOutputDevices();
    for (int i = 0; i < devices.size(); ++i) {
        monitorDeviceCombo_.addItem(devices[i], i + 1);
    }

    // Try to select the currently active output device
    if (auto* device = engine_.getDeviceManager().getCurrentAudioDevice()) {
        juce::AudioDeviceManager::AudioDeviceSetup setup;
        engine_.getDeviceManager().getAudioDeviceSetup(setup);

        int idx = devices.indexOf(setup.outputDeviceName);
        if (idx >= 0) {
            monitorDeviceCombo_.setSelectedId(idx + 1, juce::dontSendNotification);
        }
    }
}

// ─── Callbacks ──────────────────────────────────────────────────────────────

void OutputPanel::onMonitorDeviceSelected()
{
    auto selectedText = monitorDeviceCombo_.getText();
    if (selectedText.isNotEmpty()) {
        engine_.setOutputDevice(selectedText);
    }
}

void OutputPanel::onVirtualMicVolumeChanged()
{
    float volume = static_cast<float>(vmicVolumeSlider_.getValue()) / 100.0f;
    engine_.getOutputRouter().setVolume(OutputRouter::Output::VirtualMic, volume);
}

void OutputPanel::onMonitorVolumeChanged()
{
    float volume = static_cast<float>(monitorVolumeSlider_.getValue()) / 100.0f;
    engine_.getOutputRouter().setVolume(OutputRouter::Output::Monitor, volume);
}

void OutputPanel::onVirtualMicMuteToggled()
{
    bool muted = vmicMuteButton_.getToggleState();
    engine_.getOutputRouter().setEnabled(OutputRouter::Output::VirtualMic, !muted);
}

void OutputPanel::onMonitorMuteToggled()
{
    bool muted = monitorMuteButton_.getToggleState();
    engine_.getOutputRouter().setEnabled(OutputRouter::Output::Monitor, !muted);

    // Also sync the engine's monitor enabled flag
    engine_.setMonitorEnabled(!muted);
}

} // namespace directpipe
