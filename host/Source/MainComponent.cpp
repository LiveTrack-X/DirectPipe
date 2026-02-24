/**
 * @file MainComponent.cpp
 * @brief Main application component implementation (v3.2)
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

    // ── Audio Settings ──
    audioSettings_ = std::make_unique<AudioSettings>(audioEngine_);
    audioSettings_->onSettingsChanged = [this] { markSettingsDirty(); };

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
        markSettingsDirty();
    };
    addAndMakeVisible(inputGainSlider_);

    // ── Output Panel ──
    outputPanel_ = std::make_unique<OutputPanel>(audioEngine_);
    outputPanel_->onSettingsChanged = [this] { markSettingsDirty(); };

    // ── Control Settings Panel ──
    controlSettingsPanel_ = std::make_unique<ControlSettingsPanel>(*controlManager_);

    // ── Right-column Tabbed Panel ──
    rightTabs_ = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    rightTabs_->setTabBarDepth(30);
    rightTabs_->setOutline(0);

    // Tab colours
    auto tabBg = juce::Colour(0xFF2A2A40);
    rightTabs_->addTab("Audio",    tabBg, audioSettings_.release(), true);
    rightTabs_->addTab("Output",   tabBg, outputPanel_.release(), true);
    rightTabs_->addTab("Controls", tabBg, controlSettingsPanel_.release(), true);

    // Re-acquire raw pointers (TabbedComponent owns the components now)
    audioSettings_.reset();
    outputPanel_.reset();
    controlSettingsPanel_.reset();

    addAndMakeVisible(*rightTabs_);

    // ── Preset Manager ──
    presetManager_ = std::make_unique<PresetManager>(audioEngine_);

    // Auto-save chain to active slot when chain changes
    pluginChainEditor_->onChainModified = [this] {
        if (loadingSlot_) return;
        int slot = presetManager_->getActiveSlot();
        if (slot >= 0)
            presetManager_->saveSlot(slot);
        markSettingsDirty();
    };

    // Auto-save when a plugin editor window is closed (captures parameter changes)
    audioEngine_.getVSTChain().onEditorClosed = [this] {
        int slot = presetManager_->getActiveSlot();
        if (slot >= 0)
            presetManager_->saveSlot(slot);
        markSettingsDirty();
    };

    savePresetBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3A3A5A));
    savePresetBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    savePresetBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    savePresetBtn_.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Save Preset", PresetManager::getPresetsDirectory(), "*.dppreset");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                             juce::FileBrowserComponent::canSelectFiles,
                             [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File()) {
                auto target = file.withFileExtension("dppreset");
                presetManager_->savePreset(target);
            }
        });
    };
    addAndMakeVisible(savePresetBtn_);

    loadPresetBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3A3A5A));
    loadPresetBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    loadPresetBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    loadPresetBtn_.onClick = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Load Preset", PresetManager::getPresetsDirectory(), "*.dppreset");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                             [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file.existsAsFile()) {
                loadingSlot_ = true;
                presetManager_->loadPreset(file);
                loadingSlot_ = false;
                refreshUI();
                updateSlotButtonStates();
            }
        });
    };
    addAndMakeVisible(loadPresetBtn_);

    // ── Quick Preset Slot Buttons (A..E) ──
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto label = juce::String::charToString(PresetManager::slotLabel(i));
        slotButtons_[static_cast<size_t>(i)] = std::make_unique<juce::TextButton>(label);
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        btn->setClickingTogglesState(true);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A40));
        btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF7B6FFF));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFAAAAAA));
        btn->onClick = [this, i] { onSlotClicked(i); };
        addAndMakeVisible(*btn);
    }

    // ── Panic Mute Button ──
    panicMuteBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
    panicMuteBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    panicMuteBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    panicMuteBtn_.onClick = [this] {
        bool muted = !audioEngine_.isMuted();
        audioEngine_.setMuted(muted);
        auto& router = audioEngine_.getOutputRouter();
        if (muted) {
            preMuteMonitorEnabled_ = router.isEnabled(OutputRouter::Output::Monitor);
            router.setEnabled(OutputRouter::Output::VirtualCable, false);
            router.setEnabled(OutputRouter::Output::Monitor, false);
            audioEngine_.setMonitorEnabled(false);
        } else {
            router.setEnabled(OutputRouter::Output::VirtualCable, true);  // Always ON
            router.setEnabled(OutputRouter::Output::Monitor, preMuteMonitorEnabled_);
            audioEngine_.setMonitorEnabled(preMuteMonitorEnabled_);
        }
        panicMuteBtn_.setButtonText(muted ? "UNMUTE" : "PANIC MUTE");
        panicMuteBtn_.setColour(juce::TextButton::buttonColourId,
                                juce::Colour(muted ? 0xFF4CAF50u : 0xFFE05050u));
        markSettingsDirty();
    };
    addAndMakeVisible(panicMuteBtn_);

    // ── Section Labels (left column only) ──
    auto setupLabel = [](juce::Label& label, const juce::String& text) {
        label.setText(text, juce::dontSendNotification);
        label.setFont(juce::Font(16.0f, juce::Font::bold));
        label.setColour(juce::Label::textColourId, juce::Colours::white);
    };
    setupLabel(inputSectionLabel_, "INPUT");
    setupLabel(vstSectionLabel_, "VST CHAIN");
    addAndMakeVisible(inputSectionLabel_);
    addAndMakeVisible(vstSectionLabel_);

    // ── Status Bar Labels ──
    auto setupStatusLabel = [this](juce::Label& label) {
        label.setFont(juce::Font(12.0f));
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

    // Credit label
    creditLabel_.setFont(juce::Font(11.0f));
    creditLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF666680));
    creditLabel_.setText("Created by LiveTrack", juce::dontSendNotification);
    creditLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(creditLabel_);

    // Start UI update timer (30 Hz)
    startTimerHz(30);

    setSize(800, 700);

    // Auto-load last saved settings
    loadSettings();

    // First launch: auto-select slot A
    if (presetManager_->getActiveSlot() < 0) {
        loadingSlot_ = true;
        presetManager_->saveSlot(0);
        loadingSlot_ = false;
    }
    updateSlotButtonStates();
}

MainComponent::~MainComponent()
{
    stopTimer();
    saveSettings();
    dispatcher_.removeListener(this);
    controlManager_->shutdown();
    audioEngine_.shutdown();
    setLookAndFeel(nullptr);
}

// ─── Action handling ────────────────────────────────────────────────────────

void MainComponent::onAction(const ActionEvent& event)
{
    switch (event.action) {
        case Action::PluginBypass: {
            auto* slot = audioEngine_.getVSTChain().getPluginSlot(event.intParam);
            if (slot)
                audioEngine_.getVSTChain().setPluginBypassed(event.intParam, !slot->bypassed);
            break;
        }

        case Action::MasterBypass: {
            bool anyActive = false;
            for (int i = 0; i < audioEngine_.getVSTChain().getPluginCount(); ++i) {
                auto* slot = audioEngine_.getVSTChain().getPluginSlot(i);
                if (slot && !slot->bypassed) { anyActive = true; break; }
            }
            // Suppress auto-save during batch bypass toggle (save once at end)
            loadingSlot_ = true;
            for (int i = 0; i < audioEngine_.getVSTChain().getPluginCount(); ++i)
                audioEngine_.getVSTChain().setPluginBypassed(i, anyActive);
            loadingSlot_ = false;
            // Save once after all bypass changes
            int activeSlot = presetManager_->getActiveSlot();
            if (activeSlot >= 0)
                presetManager_->saveSlot(activeSlot);
            markSettingsDirty();
            break;
        }

        case Action::PanicMute:
        case Action::ToggleMute:
        case Action::InputMuteToggle: {
            bool muted = !audioEngine_.isMuted();
            audioEngine_.setMuted(muted);
            auto& router = audioEngine_.getOutputRouter();
            if (muted) {
                preMuteMonitorEnabled_ = router.isEnabled(OutputRouter::Output::Monitor);
                router.setEnabled(OutputRouter::Output::VirtualCable, false);
                router.setEnabled(OutputRouter::Output::Monitor, false);
                audioEngine_.setMonitorEnabled(false);
            } else {
                router.setEnabled(OutputRouter::Output::VirtualCable, true);  // Always ON
                router.setEnabled(OutputRouter::Output::Monitor, preMuteMonitorEnabled_);
                audioEngine_.setMonitorEnabled(preMuteMonitorEnabled_);
            }
            panicMuteBtn_.setButtonText(muted ? "UNMUTE" : "PANIC MUTE");
            panicMuteBtn_.setColour(juce::TextButton::buttonColourId,
                                    juce::Colour(muted ? 0xFF4CAF50u : 0xFFE05050u));
            markSettingsDirty();
            break;
        }

        case Action::InputGainAdjust:
            audioEngine_.setInputGain(audioEngine_.getInputGain() + event.floatParam * 0.1f);
            inputGainSlider_.setValue(audioEngine_.getInputGain(), juce::dontSendNotification);
            markSettingsDirty();
            break;

        case Action::SetVolume:
            if (event.stringParam == "monitor")
                audioEngine_.getOutputRouter().setVolume(OutputRouter::Output::Monitor, event.floatParam);
            else if (event.stringParam == "vmic")
                audioEngine_.getOutputRouter().setVolume(OutputRouter::Output::VirtualCable, event.floatParam);
            markSettingsDirty();
            break;

        case Action::SwitchPresetSlot: {
            if (loadingSlot_) break;
            int slot = event.intParam;
            // Save current slot first (captures plugin internal state)
            int currentSlot = presetManager_->getActiveSlot();
            if (currentSlot >= 0 && currentSlot != slot)
                presetManager_->saveSlot(currentSlot);
            loadingSlot_ = true;
            setSlotButtonsEnabled(false);
            presetManager_->loadSlotAsync(slot, [this](bool /*ok*/) {
                loadingSlot_ = false;
                setSlotButtonsEnabled(true);
                refreshUI();
                updateSlotButtonStates();
                markSettingsDirty();
            });
            break;
        }

        default:
            break;
    }
}

// ─── Preset Slots ───────────────────────────────────────────────────────────

void MainComponent::onSlotClicked(int slotIndex)
{
    if (loadingSlot_) return;  // Prevent double-click during load

    if (presetManager_->getActiveSlot() == slotIndex) {
        presetManager_->saveSlot(slotIndex);
    } else {
        // Save current slot first (captures plugin internal state)
        int currentSlot = presetManager_->getActiveSlot();
        if (currentSlot >= 0)
            presetManager_->saveSlot(currentSlot);

        if (presetManager_->isSlotOccupied(slotIndex)) {
            loadingSlot_ = true;
            setSlotButtonsEnabled(false);
            presetManager_->loadSlotAsync(slotIndex, [this](bool /*ok*/) {
                loadingSlot_ = false;
                setSlotButtonsEnabled(true);
                refreshUI();
                updateSlotButtonStates();
            });
            // Update button state immediately to show selection
            updateSlotButtonStates();
            return;
        } else {
            presetManager_->saveSlot(slotIndex);
        }
    }
    updateSlotButtonStates();
}

void MainComponent::updateSlotButtonStates()
{
    int active = presetManager_->getActiveSlot();
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        bool isActive = (i == active);
        bool occupied = presetManager_->isSlotOccupied(i);

        btn->setToggleState(isActive, juce::dontSendNotification);

        if (isActive) {
            btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF7B6FFF));
            btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        } else if (occupied) {
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF3A3A5A));
            btn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFFCCCCCC));
        } else {
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A40));
            btn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF666666));
        }
        btn->repaint();
    }
}

void MainComponent::setSlotButtonsEnabled(bool enabled)
{
    for (int i = 0; i < kNumPresetSlots; ++i)
        slotButtons_[static_cast<size_t>(i)]->setEnabled(enabled);
}

// ─── Paint ──────────────────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E2E));

    // Status bar background
    g.setColour(juce::Colour(0xFF15152A));
    g.fillRect(0, getHeight() - 30, getWidth(), 30);
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

    int inputStartY = y;

    // Input gain row
    inputGainLabel_.setBounds(bounds.getX(), y, 40, 24);
    inputGainSlider_.setBounds(bounds.getX() + 44, y, halfW - 94, 24);
    y += 30;

    // Input meter
    int inputMeterH = y - inputStartY;
    inputMeter_->setBounds(bounds.getX() + halfW - 45, inputStartY, 40, inputMeterH);

    // ── VST CHAIN Section ──
    vstSectionLabel_.setBounds(bounds.getX(), y, 120, 24);

    int presetBtnX = bounds.getX() + 125;
    savePresetBtn_.setBounds(presetBtnX, y, 90, 24);
    loadPresetBtn_.setBounds(presetBtnX + 94, y, 90, 24);
    y += 26;

    // Quick preset slot buttons (A..E)
    {
        int slotBtnW = (halfW - 4 * 4) / kNumPresetSlots;
        for (int i = 0; i < kNumPresetSlots; ++i) {
            slotButtons_[static_cast<size_t>(i)]->setBounds(
                bounds.getX() + i * (slotBtnW + 4), y, slotBtnW, 26);
        }
        y += 30;
    }

    int vstH = bounds.getBottom() - y - 40;
    pluginChainEditor_->setBounds(bounds.getX(), y, halfW, vstH - 34);
    y += vstH - 30;

    panicMuteBtn_.setBounds(bounds.getX(), y, halfW, 28);

    // ═══ Right Column: Tabbed Panel + Output Meter ═══
    int rx = bounds.getX() + halfW + 10;
    int rw = bounds.getWidth() - halfW - 10;
    int ry = bounds.getY();

    int tabH = bounds.getBottom() - ry - 34;

    // Output meter alongside tabs
    outputMeter_->setBounds(rx + rw - 45, ry + 30, 40, tabH - 30);

    // Tabbed panel (leaves space for output meter)
    rightTabs_->setBounds(rx, ry, rw - 50, tabH);

    // ── Status Bar ──
    int statusY = getHeight() - 28;
    int statusW = getWidth() / 4;
    latencyLabel_.setBounds(5, statusY, statusW, 24);
    cpuLabel_.setBounds(statusW, statusY, statusW, 24);
    formatLabel_.setBounds(statusW * 2, statusY, statusW, 24);
    portableLabel_.setBounds(statusW * 3, statusY, statusW, 24);
    creditLabel_.setBounds(getWidth() - 160, statusY, 150, 24);
}

// ─── Timer ──────────────────────────────────────────────────────────────────

void MainComponent::timerCallback()
{
    auto& monitor = audioEngine_.getLatencyMonitor();

    bool muted = audioEngine_.isMuted();
    inputMeter_->setLevel(audioEngine_.getInputLevel());
    outputMeter_->setLevel(muted ? 0.0f : audioEngine_.getOutputLevel());

    latencyLabel_.setText(
        "Latency: " + juce::String(monitor.getTotalLatencyVirtualMicMs(), 1) + "ms",
        juce::dontSendNotification);

    cpuLabel_.setText(
        "CPU: " + juce::String(monitor.getCpuUsagePercent(), 1) + "%",
        juce::dontSendNotification);

    formatLabel_.setText(
        juce::String(static_cast<int>(monitor.getSampleRate())) + "Hz / " +
        juce::String(monitor.getBufferSize()) + " smp / " +
        juce::String(audioEngine_.getChannelMode() == 1 ? "Mono" : "Stereo"),
        juce::dontSendNotification);

    float currentGain = audioEngine_.getInputGain();
    if (std::abs(static_cast<float>(inputGainSlider_.getValue()) - currentGain) > 0.01f) {
        inputGainSlider_.setValue(currentGain, juce::dontSendNotification);
    }

    // Dirty-flag auto-save with 1-second debounce (30 ticks at 30Hz)
    if (settingsDirty_ && dirtyCooldown_ > 0) {
        if (--dirtyCooldown_ == 0) {
            settingsDirty_ = false;
            saveSettings();
        }
    }
}

// ─── Settings auto-save/load ─────────────────────────────────────────────────

void MainComponent::markSettingsDirty()
{
    settingsDirty_ = true;
    dirtyCooldown_ = 30;  // reset debounce: save after ~1 second of inactivity
}

void MainComponent::saveSettings()
{
    // Save current slot's chain state (captures plugin internal state)
    int currentSlot = presetManager_->getActiveSlot();
    if (currentSlot >= 0)
        presetManager_->saveSlot(currentSlot);

    auto file = PresetManager::getAutoSaveFile();
    presetManager_->savePreset(file);
}

void MainComponent::loadSettings()
{
    auto file = PresetManager::getAutoSaveFile();
    if (file.existsAsFile()) {
        loadingSlot_ = true;
        presetManager_->loadPreset(file);
        loadingSlot_ = false;
        refreshUI();
    }
}

void MainComponent::refreshUI()
{
    inputGainSlider_.setValue(audioEngine_.getInputGain(), juce::dontSendNotification);

    // Get tab content components for refresh
    if (auto* audioSettingsComp = dynamic_cast<AudioSettings*>(rightTabs_->getTabContentComponent(0)))
        audioSettingsComp->refreshFromEngine();

    if (pluginChainEditor_)
        pluginChainEditor_->refreshList();

    if (auto* outputComp = dynamic_cast<OutputPanel*>(rightTabs_->getTabContentComponent(1)))
        outputComp->refreshMonitorDeviceList();

    bool muted = audioEngine_.isMuted();
    panicMuteBtn_.setButtonText(muted ? "UNMUTE" : "PANIC MUTE");
    panicMuteBtn_.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(muted ? 0xFF4CAF50u : 0xFFE05050u));
}

} // namespace directpipe
