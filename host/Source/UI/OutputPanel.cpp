/**
 * @file OutputPanel.cpp
 * @brief Output routing control panel — VirtualCable + Monitor
 */

#include "OutputPanel.h"

namespace directpipe {

// ─── Construction / Destruction ─────────────────────────────────────────────

OutputPanel::OutputPanel(AudioEngine& engine)
    : engine_(engine)
{
    auto styleTitle = [this](juce::Label& label) {
        label.setFont(juce::Font(14.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
        addAndMakeVisible(label);
    };
    auto styleLabel = [this](juce::Label& label) {
        label.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
        addAndMakeVisible(label);
    };
    auto styleSlider = [this](juce::Slider& slider) {
        slider.setSliderStyle(juce::Slider::LinearHorizontal);
        slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        slider.setRange(0.0, 100.0, 1.0);
        slider.setValue(100.0, juce::dontSendNotification);
        slider.setTextValueSuffix(" %");
        slider.setColour(juce::Slider::thumbColourId, juce::Colour(kAccentColour));
        slider.setColour(juce::Slider::trackColourId, juce::Colour(kAccentColour).withAlpha(0.4f));
        slider.setColour(juce::Slider::backgroundColourId, juce::Colour(kSurfaceColour).brighter(0.1f));
        slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour(kTextColour));
        slider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible(slider);
    };

    // ── Virtual Cable section ──
    styleTitle(vcTitleLabel_);
    styleLabel(vcDeviceLabel_);
    vcDeviceCombo_.onChange = [this] { onVCDeviceSelected(); };
    addAndMakeVisible(vcDeviceCombo_);

    styleLabel(vcVolumeLabel_);
    styleSlider(vcVolumeSlider_);
    vcVolumeSlider_.onValueChange = [this] { onVCVolumeChanged(); };

    // ── Monitor section ──
    styleTitle(monTitleLabel_);
    styleLabel(monDeviceLabel_);
    monDeviceCombo_.onChange = [this] { onMonitorDeviceSelected(); };
    addAndMakeVisible(monDeviceCombo_);

    styleLabel(monVolumeLabel_);
    styleSlider(monVolumeSlider_);
    monVolumeSlider_.onValueChange = [this] { onMonitorVolumeChanged(); };

    monEnableButton_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    monEnableButton_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
    monEnableButton_.onClick = [this] { onMonitorEnableToggled(); };
    addAndMakeVisible(monEnableButton_);

    // ── Initial state ──
    refreshDeviceLists();

    auto& router = engine_.getOutputRouter();

    // VirtualCable volume
    vcVolumeSlider_.setValue(
        static_cast<double>(router.getVolume(OutputRouter::Output::VirtualCable)) * 100.0,
        juce::dontSendNotification);

    // Monitor volume + enable
    monVolumeSlider_.setValue(
        static_cast<double>(router.getVolume(OutputRouter::Output::Monitor)) * 100.0,
        juce::dontSendNotification);
    monEnableButton_.setToggleState(
        router.isEnabled(OutputRouter::Output::Monitor),
        juce::dontSendNotification);

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

    // ── Virtual Cable title ──
    vcTitleLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    // VC device
    vcDeviceLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    vcDeviceCombo_.setBounds(bounds.getX() + labelW + gap, y,
                             bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // VC volume
    vcVolumeLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    vcVolumeSlider_.setBounds(bounds.getX() + labelW + gap, y,
                              bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap + 8;

    // ── Monitor title ──
    monTitleLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    // Monitor device
    monDeviceLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    monDeviceCombo_.setBounds(bounds.getX() + labelW + gap, y,
                              bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Monitor volume
    monVolumeLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    monVolumeSlider_.setBounds(bounds.getX() + labelW + gap, y,
                               bounds.getWidth() - labelW - gap, rowH);
    y += rowH + gap;

    // Monitor enable
    monEnableButton_.setBounds(bounds.getX() + labelW + gap, y, 120, rowH);
}

// ─── Timer callback ─────────────────────────────────────────────────────────

void OutputPanel::timerCallback()
{
    auto& router = engine_.getOutputRouter();

    bool monEnabled = router.isEnabled(OutputRouter::Output::Monitor);
    if (monEnableButton_.getToggleState() != monEnabled)
        monEnableButton_.setToggleState(monEnabled, juce::dontSendNotification);
}

// ─── Device lists ───────────────────────────────────────────────────────────

void OutputPanel::refreshDeviceLists()
{
    // VirtualCable — always WASAPI devices
    vcDeviceCombo_.clear(juce::dontSendNotification);
    auto vcDevices = engine_.getVirtualMicOutput().getAvailableOutputDevices();
    for (int i = 0; i < vcDevices.size(); ++i)
        vcDeviceCombo_.addItem(vcDevices[i], i + 1);

    auto currentVC = engine_.getVirtualCableDeviceName();
    int vcIdx = vcDevices.indexOf(currentVC);
    if (vcIdx >= 0)
        vcDeviceCombo_.setSelectedId(vcIdx + 1, juce::dontSendNotification);

    // Monitor — always WASAPI devices
    monDeviceCombo_.clear(juce::dontSendNotification);
    auto monDevices = engine_.getMonitorOutput().getAvailableOutputDevices();
    for (int i = 0; i < monDevices.size(); ++i)
        monDeviceCombo_.addItem(monDevices[i], i + 1);

    auto currentMon = engine_.getMonitorDeviceName();
    int monIdx = monDevices.indexOf(currentMon);
    if (monIdx >= 0)
        monDeviceCombo_.setSelectedId(monIdx + 1, juce::dontSendNotification);
}

// ─── VirtualCable callbacks ─────────────────────────────────────────────────

void OutputPanel::onVCDeviceSelected()
{
    auto selectedText = vcDeviceCombo_.getText();
    if (selectedText.isNotEmpty()) {
        engine_.setVirtualCableDevice(selectedText);
        if (onSettingsChanged) onSettingsChanged();
    }
}

void OutputPanel::onVCVolumeChanged()
{
    float volume = static_cast<float>(vcVolumeSlider_.getValue()) / 100.0f;
    engine_.getOutputRouter().setVolume(OutputRouter::Output::VirtualCable, volume);
    if (onSettingsChanged) onSettingsChanged();
}

// ─── Monitor callbacks ──────────────────────────────────────────────────────

void OutputPanel::onMonitorDeviceSelected()
{
    auto selectedText = monDeviceCombo_.getText();
    if (selectedText.isNotEmpty()) {
        engine_.setMonitorDevice(selectedText);
        if (onSettingsChanged) onSettingsChanged();
    }
}

void OutputPanel::onMonitorVolumeChanged()
{
    float volume = static_cast<float>(monVolumeSlider_.getValue()) / 100.0f;
    engine_.getOutputRouter().setVolume(OutputRouter::Output::Monitor, volume);
    if (onSettingsChanged) onSettingsChanged();
}

void OutputPanel::onMonitorEnableToggled()
{
    bool enabled = monEnableButton_.getToggleState();
    engine_.getOutputRouter().setEnabled(OutputRouter::Output::Monitor, enabled);
    engine_.setMonitorEnabled(enabled);
    if (onSettingsChanged) onSettingsChanged();
}

} // namespace directpipe
