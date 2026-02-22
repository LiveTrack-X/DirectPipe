/**
 * @file MainComponent.cpp
 * @brief Main application component implementation
 */

#include "MainComponent.h"

namespace directpipe {

MainComponent::MainComponent()
{
    setLookAndFeel(&lookAndFeel_);

    // Initialize audio engine
    audioEngine_.initialize();

    // ── Device Selector ──
    deviceSelector_ = std::make_unique<DeviceSelector>(audioEngine_);
    addAndMakeVisible(*deviceSelector_);

    // ── Plugin Chain Editor ──
    pluginChainEditor_ = std::make_unique<PluginChainEditor>(audioEngine_.getVSTChain());
    addAndMakeVisible(*pluginChainEditor_);

    // ── Level Meters ──
    inputMeter_ = std::make_unique<LevelMeter>("INPUT");
    outputMeterOBS_ = std::make_unique<LevelMeter>("OBS");
    outputMeterVMic_ = std::make_unique<LevelMeter>("VMic");
    outputMeterMonitor_ = std::make_unique<LevelMeter>("Mon");
    addAndMakeVisible(*inputMeter_);
    addAndMakeVisible(*outputMeterOBS_);
    addAndMakeVisible(*outputMeterVMic_);
    addAndMakeVisible(*outputMeterMonitor_);

    // ── Output Volume Sliders ──
    auto setupSlider = [this](juce::Slider& s) {
        s.setSliderStyle(juce::Slider::LinearHorizontal);
        s.setRange(0.0, 1.0, 0.01);
        s.setValue(1.0);
        s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
        addAndMakeVisible(s);
    };
    setupSlider(obsVolumeSlider_);
    setupSlider(vmicVolumeSlider_);
    setupSlider(monitorVolumeSlider_);

    obsVolumeSlider_.onValueChange = [this] {
        audioEngine_.getOutputRouter().setVolume(
            OutputRouter::Output::SharedMemory, static_cast<float>(obsVolumeSlider_.getValue()));
    };
    vmicVolumeSlider_.onValueChange = [this] {
        audioEngine_.getOutputRouter().setVolume(
            OutputRouter::Output::VirtualMic, static_cast<float>(vmicVolumeSlider_.getValue()));
    };
    monitorVolumeSlider_.onValueChange = [this] {
        audioEngine_.getOutputRouter().setVolume(
            OutputRouter::Output::Monitor, static_cast<float>(monitorVolumeSlider_.getValue()));
    };

    // ── Output Enable Toggles ──
    addAndMakeVisible(obsEnableBtn_);
    addAndMakeVisible(vmicEnableBtn_);
    addAndMakeVisible(monitorEnableBtn_);

    obsEnableBtn_.setToggleState(true, juce::dontSendNotification);
    vmicEnableBtn_.setToggleState(true, juce::dontSendNotification);
    monitorEnableBtn_.setToggleState(true, juce::dontSendNotification);

    obsEnableBtn_.onClick = [this] {
        audioEngine_.getOutputRouter().setEnabled(
            OutputRouter::Output::SharedMemory, obsEnableBtn_.getToggleState());
    };
    vmicEnableBtn_.onClick = [this] {
        audioEngine_.getOutputRouter().setEnabled(
            OutputRouter::Output::VirtualMic, vmicEnableBtn_.getToggleState());
    };
    monitorEnableBtn_.onClick = [this] {
        audioEngine_.getOutputRouter().setEnabled(
            OutputRouter::Output::Monitor, monitorEnableBtn_.getToggleState());
        audioEngine_.setMonitorEnabled(monitorEnableBtn_.getToggleState());
    };

    // ── Section Labels ──
    auto setupLabel = [](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colours::white);
    };
    setupLabel(inputSectionLabel_, "INPUT");
    setupLabel(vstSectionLabel_, "VST CHAIN");
    setupLabel(outputSectionLabel_, "OUTPUT");
    addAndMakeVisible(inputSectionLabel_);
    addAndMakeVisible(vstSectionLabel_);
    addAndMakeVisible(outputSectionLabel_);

    // ── Status Bar Labels ──
    auto setupStatusLabel = [this](juce::Label& label) {
        label.setFont(juce::FontOptions(12.0f));
        label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(label);
    };
    setupStatusLabel(latencyLabel_);
    setupStatusLabel(cpuLabel_);
    setupStatusLabel(formatLabel_);
    setupStatusLabel(obsStatusLabel_);

    // Start UI update timer (30 Hz)
    startTimerHz(30);

    setSize(700, 600);
}

MainComponent::~MainComponent()
{
    stopTimer();
    audioEngine_.shutdown();
    setLookAndFeel(nullptr);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E2E));  // Dark background

    // Draw section separators
    g.setColour(juce::Colour(0xFF3A3A5A));

    auto bounds = getLocalBounds().reduced(10);
    int inputSectionBottom = bounds.getY() + 100;
    int vstSectionBottom = inputSectionBottom + 250;

    g.drawHorizontalLine(inputSectionBottom, 10.0f, static_cast<float>(getWidth() - 10));
    g.drawHorizontalLine(vstSectionBottom, 10.0f, static_cast<float>(getWidth() - 10));

    // Status bar background
    g.setColour(juce::Colour(0xFF15152A));
    g.fillRect(0, getHeight() - 30, getWidth(), 30);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    int y = bounds.getY();

    // ── INPUT Section ──
    inputSectionLabel_.setBounds(bounds.getX(), y, 100, 24);
    y += 26;
    deviceSelector_->setBounds(bounds.getX(), y, bounds.getWidth() - 60, 60);
    inputMeter_->setBounds(bounds.getRight() - 50, y, 40, 60);
    y += 70;

    // ── VST CHAIN Section ──
    vstSectionLabel_.setBounds(bounds.getX(), y, 200, 24);
    y += 26;
    pluginChainEditor_->setBounds(bounds.getX(), y, bounds.getWidth(), 210);
    y += 220;

    // ── OUTPUT Section ──
    outputSectionLabel_.setBounds(bounds.getX(), y, 200, 24);
    y += 28;

    int outputRowHeight = 30;
    int toggleWidth = 110;
    int meterWidth = 40;
    int sliderWidth = bounds.getWidth() - toggleWidth - meterWidth - 20;

    // OBS output row
    obsEnableBtn_.setBounds(bounds.getX(), y, toggleWidth, outputRowHeight);
    outputMeterOBS_->setBounds(bounds.getX() + toggleWidth + 5, y, meterWidth, outputRowHeight);
    obsVolumeSlider_.setBounds(bounds.getX() + toggleWidth + meterWidth + 10, y, sliderWidth, outputRowHeight);
    y += outputRowHeight + 5;

    // Virtual Mic output row
    vmicEnableBtn_.setBounds(bounds.getX(), y, toggleWidth, outputRowHeight);
    outputMeterVMic_->setBounds(bounds.getX() + toggleWidth + 5, y, meterWidth, outputRowHeight);
    vmicVolumeSlider_.setBounds(bounds.getX() + toggleWidth + meterWidth + 10, y, sliderWidth, outputRowHeight);
    y += outputRowHeight + 5;

    // Monitor output row
    monitorEnableBtn_.setBounds(bounds.getX(), y, toggleWidth, outputRowHeight);
    outputMeterMonitor_->setBounds(bounds.getX() + toggleWidth + 5, y, meterWidth, outputRowHeight);
    monitorVolumeSlider_.setBounds(bounds.getX() + toggleWidth + meterWidth + 10, y, sliderWidth, outputRowHeight);

    // ── Status Bar ──
    int statusY = getHeight() - 28;
    int statusW = getWidth() / 4;
    latencyLabel_.setBounds(5, statusY, statusW, 24);
    cpuLabel_.setBounds(statusW, statusY, statusW, 24);
    formatLabel_.setBounds(statusW * 2, statusY, statusW, 24);
    obsStatusLabel_.setBounds(statusW * 3, statusY, statusW, 24);
}

void MainComponent::timerCallback()
{
    auto& monitor = audioEngine_.getLatencyMonitor();
    auto& router = audioEngine_.getOutputRouter();

    // Update level meters
    inputMeter_->setLevel(audioEngine_.getInputLevel());
    outputMeterOBS_->setLevel(router.getLevel(OutputRouter::Output::SharedMemory));
    outputMeterVMic_->setLevel(router.getLevel(OutputRouter::Output::VirtualMic));
    outputMeterMonitor_->setLevel(router.getLevel(OutputRouter::Output::Monitor));

    // Update status bar
    latencyLabel_.setText(
        "Latency: " + juce::String(monitor.getTotalLatencyOBSMs(), 1) + "ms",
        juce::dontSendNotification);

    cpuLabel_.setText(
        "CPU: " + juce::String(monitor.getCpuUsagePercent(), 1) + "%",
        juce::dontSendNotification);

    formatLabel_.setText(
        juce::String(static_cast<int>(monitor.getSampleRate())) + "Hz / " +
        juce::String(monitor.getBufferSize()) + " smp",
        juce::dontSendNotification);

    obsStatusLabel_.setText(
        router.isOBSConnected() ? "OBS: Connected" : "OBS: Waiting...",
        juce::dontSendNotification);

    obsStatusLabel_.setColour(juce::Label::textColourId,
                              router.isOBSConnected() ? juce::Colours::lightgreen : juce::Colours::yellow);
}

} // namespace directpipe
