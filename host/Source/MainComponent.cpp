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
 * @file MainComponent.cpp
 * @brief Main application component implementation (v3.2)
 */

#include "MainComponent.h"
#include "Control/ControlMapping.h"
#include "UI/SettingsExporter.h"  // Used by onSaveSettings/onLoadSettings callbacks
#include <thread>

namespace directpipe {

MainComponent::MainComponent()
{
    setLookAndFeel(&lookAndFeel_);

    // Initialize audio engine
    if (!audioEngine_.initialize()) {
        juce::MessageManager::callAsync([this] {
            showNotification("Audio engine failed to start — check device settings",
                             NotificationLevel::Critical);
        });
    }

    // Wire error callbacks
    audioEngine_.onDeviceError = [this](const juce::String& msg) {
        juce::MessageManager::callAsync([this, msg] {
            showNotification(msg, NotificationLevel::Warning);
        });
    };
    audioEngine_.getVSTChain().onPluginLoadFailed = [this](const juce::String& name, const juce::String& err) {
        juce::MessageManager::callAsync([this, name, err] {
            showNotification("Plugin load failed: " + name + " — " + err,
                             NotificationLevel::Error);
        });
    };

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
    outputPanel_->onRecordToggle = [this] {
        ActionEvent ev;
        ev.action = Action::RecordingToggle;
        dispatcher_.dispatch(ev);
    };
    outputPanelPtr_ = outputPanel_.get();

    // ── Control Settings Panel ──
    controlSettingsPanel_ = std::make_unique<ControlSettingsPanel>(
        *controlManager_, &audioEngine_.getVSTChain());

    // Settings Save/Load callbacks (General tab)
    controlSettingsPanel_->onSaveSettings = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Save Settings",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                .getChildFile("DirectPipe_backup.dpbackup"),
            "*.dpbackup");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                             juce::FileBrowserComponent::canSelectFiles,
                             [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File()) {
                auto target = file.withFileExtension("dpbackup");
                ControlMappingStore store;
                auto json = SettingsExporter::exportAll(*presetManager_, store);
                target.replaceWithText(json);
            }
        });
    };
    controlSettingsPanel_->onLoadSettings = [this] {
        auto chooser = std::make_shared<juce::FileChooser>(
            "Load Settings",
            juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
            "*.dpbackup");
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                             [this, chooser](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file.existsAsFile()) {
                auto json = file.loadFileAsString();
                ControlMappingStore store;
                loadingSlot_ = true;
                SettingsExporter::importAll(json, *presetManager_, store);
                controlManager_->reloadConfig();
                loadingSlot_ = false;
                refreshUI();
                updateSlotButtonStates();
            }
        });
    };

    // ── Right-column Tabbed Panel ──
    rightTabs_ = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    rightTabs_->setTabBarDepth(30);
    rightTabs_->setOutline(0);

    // Tab colours
    auto tabBg = juce::Colour(0xFF2A2A40);
    rightTabs_->addTab("Audio",    tabBg, audioSettings_.release(), true);
    rightTabs_->addTab("Monitor",  tabBg, outputPanel_.release(), true);
    rightTabs_->addTab("Controls", tabBg, controlSettingsPanel_.release(), true);

    // ── Log Panel ──
    {
        auto logPanel = std::make_unique<LogPanel>();
        logPanel->onResetSettings = [this] {
            loadingSlot_ = true;
            controlManager_->reloadConfig();
            loadSettings();
            loadingSlot_ = false;
            refreshUI();
            updateSlotButtonStates();
        };
        rightTabs_->addTab("Log", tabBg, logPanel.release(), true);
    }

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

    // ── Mute Status Indicators (clickable) ──
    auto setupMuteBtn = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF4CAF50));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(btn);
    };
    setupMuteBtn(outputMuteBtn_);
    setupMuteBtn(monitorMuteBtn_);
    outputMuteBtn_.onClick = [this] {
        bool outputMuted = !audioEngine_.isOutputMuted();
        audioEngine_.setOutputMuted(outputMuted);
        markSettingsDirty();
    };
    monitorMuteBtn_.onClick = [this] {
        auto& router = audioEngine_.getOutputRouter();
        bool enabled = !router.isEnabled(OutputRouter::Output::Monitor);
        router.setEnabled(OutputRouter::Output::Monitor, enabled);
        audioEngine_.setMonitorEnabled(enabled);
        markSettingsDirty();
    };

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
            preMuteOutputMuted_ = audioEngine_.isOutputMuted();
            audioEngine_.setOutputMuted(false);
            router.setEnabled(OutputRouter::Output::Monitor, false);
            audioEngine_.setMonitorEnabled(false);
        } else {
            audioEngine_.setOutputMuted(preMuteOutputMuted_);
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

    // Credit + version hyperlink (click opens GitHub)
    creditLink_.setButtonText("v" + juce::String(ProjectInfo::versionString) + " | Created by LiveTrack");
    creditLink_.setFont(juce::Font(11.0f), false);
    creditLink_.setColour(juce::HyperlinkButton::textColourId, juce::Colour(0xFF666680));
    creditLink_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(creditLink_);

    // ── Notification Bar (overlays status bar labels on error) ──
    addAndMakeVisible(notificationBar_);
    notificationBar_.setVisible(false);

    // Start UI update timer (30 Hz)
    startTimerHz(30);

    setSize(kDefaultWidth, kDefaultHeight);

    // Auto-load last saved settings
    loadSettings();

    // First launch: auto-select slot A
    if (presetManager_->getActiveSlot() < 0) {
        loadingSlot_ = true;
        presetManager_->saveSlot(0);
        loadingSlot_ = false;
    }
    updateSlotButtonStates();

    // Check for new release on GitHub (background thread)
    checkForUpdate();
}

MainComponent::~MainComponent()
{
    stopTimer();
    if (updateCheckThread_.joinable())
        updateCheckThread_.join();
    saveSettings();
    dispatcher_.removeListener(this);
    controlManager_->shutdown();
    audioEngine_.shutdown();
    setLookAndFeel(nullptr);
}

// ─── Action handling ────────────────────────────────────────────────────────

void MainComponent::onAction(const ActionEvent& event)
{
    // ActionDispatcher guarantees message-thread delivery, so no thread check needed.
    handleAction(event);
}

void MainComponent::handleAction(const ActionEvent& event)
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

        case Action::ToggleMute: {
            if (event.stringParam == "monitor") {
                // Monitor mute = toggle monitor enable (headphones only)
                auto& router = audioEngine_.getOutputRouter();
                bool enabled = !router.isEnabled(OutputRouter::Output::Monitor);
                router.setEnabled(OutputRouter::Output::Monitor, enabled);
                audioEngine_.setMonitorEnabled(enabled);
            } else if (event.stringParam == "output") {
                // Output mute = silence main output only, monitor keeps working
                bool outputMuted = !audioEngine_.isOutputMuted();
                audioEngine_.setOutputMuted(outputMuted);
            } else {
                // Input / global mute (same as panic)
                bool muted = !audioEngine_.isMuted();
                audioEngine_.setMuted(muted);
                auto& router = audioEngine_.getOutputRouter();
                if (muted) {
                    preMuteMonitorEnabled_ = router.isEnabled(OutputRouter::Output::Monitor);
                    router.setEnabled(OutputRouter::Output::Monitor, false);
                    audioEngine_.setMonitorEnabled(false);
                } else {
                    router.setEnabled(OutputRouter::Output::Monitor, preMuteMonitorEnabled_);
                    audioEngine_.setMonitorEnabled(preMuteMonitorEnabled_);
                }
                panicMuteBtn_.setButtonText(muted ? "UNMUTE" : "PANIC MUTE");
                panicMuteBtn_.setColour(juce::TextButton::buttonColourId,
                                        juce::Colour(muted ? 0xFF4CAF50u : 0xFFE05050u));
            }
            markSettingsDirty();
            break;
        }

        case Action::PanicMute:
        case Action::InputMuteToggle: {
            bool muted = !audioEngine_.isMuted();
            audioEngine_.setMuted(muted);
            auto& router = audioEngine_.getOutputRouter();
            if (muted) {
                preMuteMonitorEnabled_ = router.isEnabled(OutputRouter::Output::Monitor);
                preMuteOutputMuted_ = audioEngine_.isOutputMuted();
                audioEngine_.setOutputMuted(false);
                router.setEnabled(OutputRouter::Output::Monitor, false);
                audioEngine_.setMonitorEnabled(false);
            } else {
                audioEngine_.setOutputMuted(preMuteOutputMuted_);
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
            else if (event.stringParam == "input") {
                audioEngine_.setInputGain(event.floatParam);
                inputGainSlider_.setValue(event.floatParam, juce::dontSendNotification);
            }
            markSettingsDirty();
            break;

        case Action::MonitorToggle: {
            auto& router = audioEngine_.getOutputRouter();
            bool enabled = !router.isEnabled(OutputRouter::Output::Monitor);
            router.setEnabled(OutputRouter::Output::Monitor, enabled);
            audioEngine_.setMonitorEnabled(enabled);
            markSettingsDirty();
            break;
        }

        case Action::RecordingToggle: {
            auto& recorder = audioEngine_.getRecorder();
            if (recorder.isRecording()) {
                auto lastFile = recorder.getRecordingFile();
                recorder.stopRecording();
                if (outputPanelPtr_)
                    outputPanelPtr_->setLastRecordedFile(lastFile);
            } else {
                auto& monitor = audioEngine_.getLatencyMonitor();
                double sr = monitor.getSampleRate();
                if (sr <= 0.0) {
                    juce::Logger::writeToLog("Recording: no audio device active");
                    break;
                }
                auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
                auto dir = outputPanelPtr_ ? outputPanelPtr_->getRecordingFolder()
                    : juce::File::getSpecialLocation(
                        juce::File::userDocumentsDirectory).getChildFile("DirectPipe Recordings");
                dir.createDirectory();
                auto file = dir.getChildFile("DirectPipe_" + timestamp + ".wav");
                if (!recorder.startRecording(file, sr, audioEngine_.getChannelMode()))
                    showNotification("Recording failed — check folder permissions",
                                     NotificationLevel::Error);
            }
            break;
        }

        case Action::SetPluginParameter: {
            audioEngine_.getVSTChain().setPluginParameter(
                event.intParam, event.intParam2, event.floatParam);
            break;
        }

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

        case Action::NextPreset:
        case Action::PreviousPreset: {
            if (loadingSlot_) break;
            int currentSlot = presetManager_->getActiveSlot();
            if (currentSlot >= 0)
                presetManager_->saveSlot(currentSlot);
            int nextSlot = (event.action == Action::NextPreset)
                ? (currentSlot + 1) % kNumPresetSlots
                : (currentSlot - 1 + kNumPresetSlots) % kNumPresetSlots;
            loadingSlot_ = true;
            setSlotButtonsEnabled(false);
            presetManager_->loadSlotAsync(nextSlot, [this](bool /*ok*/) {
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
            btn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF999999));
        }
        btn->repaint();
    }
}

void MainComponent::setSlotButtonsEnabled(bool enabled)
{
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        btn->setEnabled(enabled);
        if (!enabled)
            btn->setAlpha(0.5f);
        else
            btn->setAlpha(1.0f);
    }
}

// ─── Paint ──────────────────────────────────────────────────────────────────

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xFF1E1E2E));

    // Status bar background
    g.setColour(juce::Colour(0xFF15152A));
    g.fillRect(0, getHeight() - kStatusBarHeight, getWidth(), kStatusBarHeight);
}

// ─── Layout ─────────────────────────────────────────────────────────────────

void MainComponent::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    int halfW = bounds.getWidth() / 2 - 5;

    // ═══ Left Column: Input Meter (left edge) + Controls ═══
    int lx = bounds.getX();
    int ly = bounds.getY();

    // Input meter: full height on left edge (like output meter on right)
    int meterAreaBottom = bounds.getBottom() - 34;
    inputMeter_->setBounds(lx, ly, kMeterWidth, meterAreaBottom - ly);

    // Content area starts after the meter
    int cx = lx + kMeterWidth + 8;
    int cw = halfW - kMeterWidth - 8;
    int y = ly;

    // ── INPUT Section ──
    inputSectionLabel_.setBounds(cx, y, 100, 24);
    y += 26;

    // Input gain row
    inputGainLabel_.setBounds(cx, y, 40, 24);
    inputGainSlider_.setBounds(cx + 44, y, cw - 44, 24);
    y += 30;

    // ── VST CHAIN Section ──
    {
        // Layout: [LABEL] [Save][Load]
        int lblW = 70;
        int btnGap = 2;
        int availW = cw - lblW - btnGap;
        int nBtns = 2;
        int bw = (availW - btnGap * (nBtns - 1)) / nBtns;

        vstSectionLabel_.setBounds(cx, y, lblW, 24);
        int bx = cx + lblW + btnGap;
        savePresetBtn_.setBounds(bx, y, bw, 24);
        bx += bw + btnGap;
        loadPresetBtn_.setBounds(bx, y, bw, 24);
    }
    y += 26;

    // Quick preset slot buttons (A..E)
    {
        int slotBtnW = (cw - kSlotBtnGap * (kNumPresetSlots - 1)) / kNumPresetSlots;
        for (int i = 0; i < kNumPresetSlots; ++i) {
            slotButtons_[static_cast<size_t>(i)]->setBounds(
                cx + i * (slotBtnW + kSlotBtnGap), y, slotBtnW, 26);
        }
        y += 30;
    }

    int vstH = bounds.getBottom() - y - 40;
    pluginChainEditor_->setBounds(cx, y, cw, vstH - 60);
    y += vstH - 56;

    // Mute status indicators above panic mute button (clickable)
    {
        int indicatorW = (cw - 4) / 2;
        outputMuteBtn_.setBounds(cx, y, indicatorW, 20);
        monitorMuteBtn_.setBounds(cx + indicatorW + 4, y, indicatorW, 20);
        y += 24;
    }

    panicMuteBtn_.setBounds(cx, y, cw, 28);

    // ═══ Right Column: Tabbed Panel + Output Meter ═══
    int rx = bounds.getX() + halfW + 10;
    int rw = bounds.getWidth() - halfW - 10;
    int ry = bounds.getY();

    int tabH = bounds.getBottom() - ry - 34;

    // Output meter alongside tabs
    outputMeter_->setBounds(rx + rw - (kMeterWidth + 5), ry + 30, kMeterWidth, tabH - 30);

    // Tabbed panel (leaves space for output meter)
    rightTabs_->setBounds(rx, ry, rw - 50, tabH);

    // ── Status Bar ──
    int statusY = getHeight() - kStatusBarHeight + 3;
    int infoW = getWidth() / 2;   // left half for latency/cpu/format
    latencyLabel_.setBounds(5, statusY, infoW * 4 / 10, 24);
    cpuLabel_.setBounds(5 + infoW * 4 / 10, statusY, infoW * 2 / 10, 24);
    formatLabel_.setBounds(5 + infoW * 6 / 10, statusY, infoW * 4 / 10, 24);
    portableLabel_.setBounds(5 + infoW, statusY, 100, 24);
    creditLink_.setBounds(getWidth() - 300, statusY, 290, 24);
    notificationBar_.setBounds(0, statusY - 3, getWidth(), kStatusBarHeight);
}

// ─── Timer ──────────────────────────────────────────────────────────────────

void MainComponent::timerCallback()
{
    // ── Drain notification queue ──
    {
        PendingNotification notif;
        while (audioEngine_.popNotification(notif))
            showNotification(notif.message, notif.level);

        notificationBar_.tick();
        bool notifActive = notificationBar_.isActive();
        latencyLabel_.setVisible(!notifActive);
        cpuLabel_.setVisible(!notifActive);
        formatLabel_.setVisible(!notifActive);
        portableLabel_.setVisible(!notifActive);
    }

    // ── Flush log entries to Log tab ──
    if (auto* logComp = dynamic_cast<LogPanel*>(rightTabs_->getTabContentComponent(3)))
        logComp->flushPendingLogs();

    auto& monitor = audioEngine_.getLatencyMonitor();

    bool muted = audioEngine_.isMuted();
    inputMeter_->setLevel(audioEngine_.getInputLevel());
    outputMeter_->setLevel(muted ? 0.0f : audioEngine_.getOutputLevel());

    // Update mute indicator colours (cached to avoid redundant repaints)
    {
        bool outMuted = audioEngine_.isOutputMuted() || muted;
        if (outMuted != cachedOutputMuted_) {
            cachedOutputMuted_ = outMuted;
            outputMuteBtn_.setColour(juce::TextButton::buttonColourId,
                outMuted ? juce::Colour(0xFFE05050) : juce::Colour(0xFF4CAF50));
        }

        bool monMuted = !audioEngine_.getOutputRouter().isEnabled(OutputRouter::Output::Monitor) || muted;
        if (monMuted != cachedMonitorMuted_) {
            cachedMonitorMuted_ = monMuted;
            monitorMuteBtn_.setColour(juce::TextButton::buttonColourId,
                monMuted ? juce::Colour(0xFFE05050) : juce::Colour(0xFF4CAF50));
        }
    }

    // Main output latency: input buffer + processing + output buffer
    double mainLatency = monitor.getTotalLatencyVirtualMicMs();

    // Monitor output latency
    auto& monOut = audioEngine_.getMonitorOutput();
    auto& router = audioEngine_.getOutputRouter();
    bool monEnabled = router.isEnabled(OutputRouter::Output::Monitor);

    juce::String latencyText = "Latency: " + juce::String(mainLatency, 1) + "ms";
    if (monEnabled) {
        double monitorLatency = mainLatency;
        if (monOut.isActive()) {
            // Separate monitor device: add its buffer latency
            double monSR = monOut.getActualSampleRate();
            if (monSR > 0.0)
                monitorLatency += (static_cast<double>(monOut.getActualBufferSize()) / monSR) * 1000.0;
        }
        latencyText += " | Mon: " + juce::String(monitorLatency, 1) + "ms";
    }

    latencyLabel_.setText(latencyText, juce::dontSendNotification);

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

    // Broadcast state to WebSocket clients (Stream Deck, etc.)
    auto& chain = audioEngine_.getVSTChain();
    broadcaster_.updateState([&](AppState& s) {
        s.inputGain = audioEngine_.getInputGain();
        s.monitorVolume = router.getVolume(OutputRouter::Output::Monitor);
        s.muted = muted;
        s.outputMuted = audioEngine_.isOutputMuted();
        s.inputMuted = muted;
        s.masterBypassed = false;
        s.latencyMs = static_cast<float>(mainLatency);
        if (monEnabled) {
            double monitorLat = mainLatency;
            if (monOut.isActive()) {
                double monSR2 = monOut.getActualSampleRate();
                if (monSR2 > 0.0)
                    monitorLat += (static_cast<double>(monOut.getActualBufferSize()) / monSR2) * 1000.0;
            }
            s.monitorLatencyMs = static_cast<float>(monitorLat);
        } else {
            s.monitorLatencyMs = 0.0f;
        }
        s.inputLevelDb = audioEngine_.getInputLevel();
        s.cpuPercent = static_cast<float>(monitor.getCpuUsagePercent());
        s.sampleRate = monitor.getSampleRate();
        s.bufferSize = monitor.getBufferSize();
        s.channelMode = audioEngine_.getChannelMode();
        s.monitorEnabled = router.isEnabled(OutputRouter::Output::Monitor);
        s.activeSlot = presetManager_ ? presetManager_->getActiveSlot() : 0;
        s.recording = audioEngine_.getRecorder().isRecording();
        s.recordingSeconds = audioEngine_.getRecorder().getRecordedSeconds();

        s.plugins.clear();
        for (int i = 0; i < chain.getPluginCount(); ++i) {
            auto* slot = chain.getPluginSlot(i);
            if (slot) {
                AppState::PluginState ps;
                ps.name = slot->name.toStdString();
                ps.bypassed = slot->bypassed;
                ps.loaded = slot->instance != nullptr;
                s.plugins.push_back(ps);
                if (!ps.bypassed && ps.loaded) s.masterBypassed = false;
            }
        }
    });

    // Update recording state in OutputPanel (Monitor tab)
    if (outputPanelPtr_) {
        bool isRec = audioEngine_.getRecorder().isRecording();
        double recSecs = audioEngine_.getRecorder().getRecordedSeconds();
        outputPanelPtr_->updateRecordingState(isRec, recSecs);
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
        outputComp->refreshDeviceLists();

    bool muted = audioEngine_.isMuted();
    panicMuteBtn_.setButtonText(muted ? "UNMUTE" : "PANIC MUTE");
    panicMuteBtn_.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(muted ? 0xFF4CAF50u : 0xFFE05050u));
}

// ─── Notification ─────────────────────────────────────────────────────────

void MainComponent::showNotification(const juce::String& message, NotificationLevel level)
{
    int durationTicks;
    switch (level) {
        case NotificationLevel::Critical: durationTicks = 240; break; // 8s
        case NotificationLevel::Error:    durationTicks = 150; break; // 5s
        case NotificationLevel::Warning:  durationTicks = 120; break; // 4s
        default:                          durationTicks = 90;  break; // 3s
    }
    notificationBar_.showNotification(message, level, durationTicks);
    juce::Logger::writeToLog("[Notification] " + message);
}

// ─── Update Check ────────────────────────────────────────────────────────────

void MainComponent::checkForUpdate()
{
    // Parse current version components
    auto currentVersion = juce::String(ProjectInfo::versionString);

    updateCheckThread_ = std::thread([this, currentVersion]() {
        juce::URL url("https://api.github.com/repos/LiveTrack-X/DirectPipe/releases/latest");
        auto response = url.readEntireTextStream(false);
        if (response.isEmpty()) return;

        // Simple JSON parse for "tag_name"
        auto parsed = juce::JSON::parse(response);
        if (auto* obj = parsed.getDynamicObject()) {
            auto tagName = obj->getProperty("tag_name").toString();
            // Strip leading 'v' if present
            if (tagName.startsWithChar('v') || tagName.startsWithChar('V'))
                tagName = tagName.substring(1);

            // Compare versions (simple string compare works for semver)
            if (tagName.isNotEmpty() && tagName != currentVersion) {
                // Check if remote is actually newer (parse major.minor.patch)
                auto parseVer = [](const juce::String& v) -> std::tuple<int,int,int> {
                    auto parts = juce::StringArray::fromTokens(v, ".", "");
                    return { parts.size() > 0 ? parts[0].getIntValue() : 0,
                             parts.size() > 1 ? parts[1].getIntValue() : 0,
                             parts.size() > 2 ? parts[2].getIntValue() : 0 };
                };
                auto [rMaj, rMin, rPat] = parseVer(tagName);
                auto [cMaj, cMin, cPat] = parseVer(currentVersion);

                bool isNewer = std::tie(rMaj, rMin, rPat) > std::tie(cMaj, cMin, cPat);
                if (isNewer) {
                    juce::MessageManager::callAsync([this, tagName]() {
                        creditLink_.setButtonText(
                            "NEW v" + tagName + " | v" +
                            juce::String(ProjectInfo::versionString) + " | Created by LiveTrack");
                        creditLink_.setColour(juce::HyperlinkButton::textColourId,
                                              juce::Colour(0xFFFFAA33)); // orange highlight
                    });
                }
            }
        }
    });
}

} // namespace directpipe
