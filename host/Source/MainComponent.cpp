/**
 * @file MainComponent.cpp
 * @brief Main application component implementation (v3.1)
 */

#include "MainComponent.h"
#include "Control/ControlMapping.h"

namespace directpipe {

MainComponent::MainComponent()
{
    setLookAndFeel(&lookAndFeel_);

    // Initialize audio engine
    audioEngine_.initialize();

    // Initialize external control system
    controlManager_ = std::make_unique<ControlManager>(dispatcher_, broadcaster_);
    controlManager_->initialize();
    dispatcher_.addListener(this);

    // ── Device Selector ──
    deviceSelector_ = std::make_unique<DeviceSelector>(audioEngine_);
    addAndMakeVisible(*deviceSelector_);

    // ── Audio Settings (v3.1: sample rate, buffer, channel mode) ──
    audioSettings_ = std::make_unique<AudioSettings>(audioEngine_);
    addAndMakeVisible(*audioSettings_);

    // ── Plugin Chain Editor ──
    pluginChainEditor_ = std::make_unique<PluginChainEditor>(audioEngine_.getVSTChain());
    addAndMakeVisible(*pluginChainEditor_);

    // ── Level Meters ──
    inputMeter_ = std::make_unique<LevelMeter>("INPUT");
    outputMeter_ = std::make_unique<LevelMeter>("OUTPUT");
    addAndMakeVisible(*inputMeter_);
    addAndMakeVisible(*outputMeter_);

    // ── Input Gain Slider ──
    inputGainLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(inputGainLabel_);

    inputGainSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    inputGainSlider_.setRange(0.0, 2.0, 0.01);
    inputGainSlider_.setValue(1.0);
    inputGainSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 20);
    inputGainSlider_.onValueChange = [this] {
        audioEngine_.setInputGain(static_cast<float>(inputGainSlider_.getValue()));
    };
    addAndMakeVisible(inputGainSlider_);

    // ── Output Panel (v3.1: Virtual Loop Mic + Monitor) ──
    outputPanel_ = std::make_unique<OutputPanel>(audioEngine_);
    addAndMakeVisible(*outputPanel_);

    // ── Control Settings Panel (v3.1: Hotkeys, MIDI, Stream Deck) ──
    controlSettingsPanel_ = std::make_unique<ControlSettingsPanel>(*controlManager_);
    addAndMakeVisible(*controlSettingsPanel_);

    // ── Panic Mute Button ──
    panicMuteBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
    panicMuteBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    panicMuteBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    panicMuteBtn_.onClick = [this] {
        bool muted = !audioEngine_.isMuted();
        audioEngine_.setMuted(muted);
        panicMuteBtn_.setButtonText(muted ? "UNMUTE" : "PANIC MUTE");
        panicMuteBtn_.setColour(juce::TextButton::buttonColourId,
                                juce::Colour(muted ? 0xFF4CAF50u : 0xFFE05050u));
    };
    addAndMakeVisible(panicMuteBtn_);

    // ── Section Labels ──
    auto setupLabel = [](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::FontOptions(16.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colours::white);
    };
    setupLabel(inputSectionLabel_, "INPUT");
    setupLabel(vstSectionLabel_, "VST CHAIN");
    setupLabel(outputSectionLabel_, "OUTPUT");
    setupLabel(controlSectionLabel_, "CONTROLS");
    addAndMakeVisible(inputSectionLabel_);
    addAndMakeVisible(vstSectionLabel_);
    addAndMakeVisible(outputSectionLabel_);
    addAndMakeVisible(controlSectionLabel_);

    // ── Status Bar Labels ──
    auto setupStatusLabel = [this](juce::Label& label) {
        label.setFont(juce::FontOptions(12.0f));
        label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(label);
    };
    setupStatusLabel(latencyLabel_);
    setupStatusLabel(cpuLabel_);
    setupStatusLabel(formatLabel_);
    setupStatusLabel(portableLabel_);

    // Show portable mode indicator
    if (ControlMappingStore::isPortableMode()) {
        portableLabel_.setText("Portable Mode", juce::dontSendNotification);
        portableLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF6C63FF));
    }

    // Start UI update timer (30 Hz)
    startTimerHz(30);

    setSize(800, 750);
}

MainComponent::~MainComponent()
{
    stopTimer();
    dispatcher_.removeListener(this);
    controlManager_->shutdown();
    audioEngine_.shutdown();
    setLookAndFeel(nullptr);
}

// ─── Action handling ────────────────────────────────────────────────────────

void MainComponent::onAction(const ActionEvent& event)
{
    switch (event.action) {
        case Action::PluginBypass:
            audioEngine_.getVSTChain().setPluginBypassed(event.intParam,
                !audioEngine_.getVSTChain().getPluginSlot(event.intParam)->bypassed);
            break;

        case Action::MasterBypass:
            // Toggle all plugins bypass
            for (int i = 0; i < audioEngine_.getVSTChain().getPluginCount(); ++i) {
                auto* slot = audioEngine_.getVSTChain().getPluginSlot(i);
                if (slot) audioEngine_.getVSTChain().setPluginBypassed(i, !slot->bypassed);
            }
            break;

        case Action::PanicMute:
            audioEngine_.setMuted(true);
            panicMuteBtn_.setButtonText("UNMUTE");
            panicMuteBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF4CAF50));
            break;

        case Action::ToggleMute:
        case Action::InputMuteToggle: {
            bool muted = !audioEngine_.isMuted();
            audioEngine_.setMuted(muted);
            panicMuteBtn_.setButtonText(muted ? "UNMUTE" : "PANIC MUTE");
            panicMuteBtn_.setColour(juce::TextButton::buttonColourId,
                                    juce::Colour(muted ? 0xFF4CAF50u : 0xFFE05050u));
            break;
        }

        case Action::InputGainAdjust:
            audioEngine_.setInputGain(audioEngine_.getInputGain() + event.floatParam * 0.1f);
            inputGainSlider_.setValue(audioEngine_.getInputGain(), juce::dontSendNotification);
            break;

        case Action::SetVolume:
            if (event.stringParam == "monitor")
                audioEngine_.getOutputRouter().setVolume(OutputRouter::Output::Monitor, event.floatParam);
            else if (event.stringParam == "vmic")
                audioEngine_.getOutputRouter().setVolume(OutputRouter::Output::VirtualMic, event.floatParam);
            break;

        default:
            break;
    }
}

// ─── Paint ──────────────────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E2E));  // Dark background

    // Draw section separators
    g.setColour(juce::Colour(0xFF3A3A5A));
    int w = getWidth();
    int halfW = w / 2;

    // Left column separators
    g.drawHorizontalLine(130, 10.0f, static_cast<float>(halfW - 5));
    g.drawHorizontalLine(370, 10.0f, static_cast<float>(halfW - 5));

    // Status bar background
    g.setColour(juce::Colour(0xFF15152A));
    g.fillRect(0, getHeight() - 30, w, 30);
}

// ─── Layout ─────────────────────────────────────────────────────────────────

void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    int halfW = bounds.getWidth() / 2 - 5;

    // ═══ Left Column: Input + VST Chain ═══
    int y = bounds.getY();

    // ── INPUT Section ──
    inputSectionLabel_.setBounds(bounds.getX(), y, 100, 24);
    y += 26;

    deviceSelector_->setBounds(bounds.getX(), y, halfW - 50, 50);
    inputMeter_->setBounds(bounds.getX() + halfW - 45, y, 40, 50);
    y += 56;

    // Input gain row
    inputGainLabel_.setBounds(bounds.getX(), y, 40, 24);
    inputGainSlider_.setBounds(bounds.getX() + 44, y, halfW - 44, 24);
    y += 30;

    // ── VST CHAIN Section ──
    vstSectionLabel_.setBounds(bounds.getX(), y, 200, 24);
    y += 26;

    int vstH = bounds.getBottom() - y - 40; // leave room for status bar + mute btn
    pluginChainEditor_->setBounds(bounds.getX(), y, halfW, vstH - 34);
    y += vstH - 30;

    panicMuteBtn_.setBounds(bounds.getX(), y, halfW, 28);

    // ═══ Right Column: Audio Settings + Output + Controls ═══
    int rx = bounds.getX() + halfW + 10;
    int rw = bounds.getWidth() - halfW - 10;
    int ry = bounds.getY();

    // Audio Settings
    audioSettings_->setBounds(rx, ry, rw, 160);
    ry += 164;

    // Output Panel
    outputSectionLabel_.setBounds(rx, ry, rw, 24);
    ry += 26;
    outputMeter_->setBounds(rx + rw - 45, ry, 40, 150);
    outputPanel_->setBounds(rx, ry, rw - 50, 150);
    ry += 156;

    // Control Settings
    controlSectionLabel_.setBounds(rx, ry, rw, 24);
    ry += 26;

    int controlH = bounds.getBottom() - ry - 34;
    controlSettingsPanel_->setBounds(rx, ry, rw, controlH);

    // ── Status Bar ──
    int statusY = getHeight() - 28;
    int statusW = getWidth() / 4;
    latencyLabel_.setBounds(5, statusY, statusW, 24);
    cpuLabel_.setBounds(statusW, statusY, statusW, 24);
    formatLabel_.setBounds(statusW * 2, statusY, statusW, 24);
    portableLabel_.setBounds(statusW * 3, statusY, statusW, 24);
}

// ─── Timer ──────────────────────────────────────────────────────────────────

void MainComponent::timerCallback()
{
    auto& monitor = audioEngine_.getLatencyMonitor();

    // Update level meters
    inputMeter_->setLevel(audioEngine_.getInputLevel());
    outputMeter_->setLevel(audioEngine_.getOutputLevel());

    // Update status bar
    latencyLabel_.setText(
        "Latency: " + juce::String(monitor.getTotalLatencyOBSMs(), 1) + "ms",
        juce::dontSendNotification);

    cpuLabel_.setText(
        "CPU: " + juce::String(monitor.getCpuUsagePercent(), 1) + "%",
        juce::dontSendNotification);

    formatLabel_.setText(
        juce::String(static_cast<int>(monitor.getSampleRate())) + "Hz / " +
        juce::String(monitor.getBufferSize()) + " smp / " +
        juce::String(audioEngine_.getChannelMode() == 1 ? "Mono" : "Stereo"),
        juce::dontSendNotification);

    // Update input gain slider if changed externally
    float currentGain = audioEngine_.getInputGain();
    if (std::abs(static_cast<float>(inputGainSlider_.getValue()) - currentGain) > 0.01f) {
        inputGainSlider_.setValue(currentGain, juce::dontSendNotification);
    }
}

} // namespace directpipe
