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

namespace {
    constexpr const char* kUpdateBatchFile = "_update.bat";
    constexpr const char* kUpdateDir       = "_update";
    constexpr const char* kUpdateZip       = "DirectPipe_update.zip";
    constexpr const char* kUpdateExe       = "DirectPipe_update.exe";
    constexpr const char* kBackupExe       = "DirectPipe_backup.exe";
    constexpr const char* kUpdatedFlag     = "_updated.flag";
}

namespace directpipe {

MainComponent::MainComponent(bool enableExternalControls)
{
    juce::LookAndFeel::setDefaultLookAndFeel(&lookAndFeel_);
    setLookAndFeel(&lookAndFeel_);

    // Initialize audio engine
    if (!audioEngine_.initialize()) {
        auto safeThis = juce::Component::SafePointer<MainComponent>(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis)
                safeThis->showNotification("Audio engine failed to start - check device settings",
                                           NotificationLevel::Critical);
        });
    }

    // Wire error callbacks (use SafePointer to guard callAsync lifetime)
    audioEngine_.onDeviceError = [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::String& msg) {
        juce::MessageManager::callAsync([safeThis, msg] {
            if (safeThis) safeThis->showNotification(msg, NotificationLevel::Warning);
        });
    };
    audioEngine_.getVSTChain().onPluginLoadFailed = [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::String& name, const juce::String& err) {
        juce::MessageManager::callAsync([safeThis, name, err] {
            if (safeThis) safeThis->showNotification("Plugin load failed: " + name + " - " + err,
                             NotificationLevel::Error);
        });
    };
    audioEngine_.onDeviceReconnected = [safeThis = juce::Component::SafePointer<MainComponent>(this)]() {
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis)
                safeThis->refreshUI();
        });
    };

    // In audio-only mode, also block IPC (shared memory name would conflict)
    if (!enableExternalControls)
        audioEngine_.setIpcAllowed(false);

    // Initialize external control system
    controlManager_ = std::make_unique<ControlManager>(dispatcher_, broadcaster_);
    controlManager_->initialize(enableExternalControls);
    dispatcher_.addListener(this);

    // ── Audio Settings ──
    audioSettings_ = std::make_unique<AudioSettings>(audioEngine_);
    audioSettings_->onSettingsChanged = [this] {
        markSettingsDirty();
        // Only invalidate preload cache if SR/BS actually changed (device-only changes keep cache valid)
        auto* device = audioEngine_.getDeviceManager().getCurrentAudioDevice();
        if (device && presetManager_) {
            double sr = device->getCurrentSampleRate();
            int bs = device->getCurrentBufferSizeSamples();
            if (sr != lastCachedSR_ || bs != lastCachedBS_) {
                lastCachedSR_ = sr;
                lastCachedBS_ = bs;
                presetManager_->invalidatePreloadCache();
            }
        }
    };

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

    outputPanel_->onIpcToggle = [this](bool enabled) {
        audioEngine_.setIpcEnabled(enabled);
        markSettingsDirty();
    };

    // ── Right-column Tabbed Panel ──
    rightTabs_ = std::make_unique<juce::TabbedComponent>(juce::TabbedButtonBar::TabsAtTop);
    rightTabs_->setTabBarDepth(30);
    rightTabs_->setOutline(0);

    // Tab colours
    auto tabBg = juce::Colour(0xFF2A2A40);
    rightTabs_->addTab("Audio",    tabBg, audioSettings_.release(), true);
    rightTabs_->addTab("Output",   tabBg, outputPanel_.release(), true);
    rightTabs_->addTab("Controls", tabBg, controlSettingsPanel_.release(), true);

    // ── Settings Panel (formerly Log + General) ──
    {
        auto settingsPanel = std::make_unique<LogPanel>();
        settingsPanel->onPresetsCleared = [this] {
            loadingSlot_ = true;   // suppress auto-save from recreating deleted slots
            // Clear the active chain
            auto& chain = audioEngine_.getVSTChain();
            for (int i = chain.getPluginCount() - 1; i >= 0; --i)
                chain.removePlugin(i);
            presetManager_->setActiveSlot(-1);
            presetManager_->refreshSlotOccupancy();
            presetManager_->invalidatePreloadCache();
            loadingSlot_ = false;
            markSettingsDirty();
            updateSlotButtonStates();
            pluginChainEditor_->refreshList();
        };
        settingsPanel->onResetSettings = [this] {
            loadingSlot_ = true;
            // Clear active chain
            auto& chain = audioEngine_.getVSTChain();
            for (int i = chain.getPluginCount() - 1; i >= 0; --i)
                chain.removePlugin(i);
            presetManager_->setActiveSlot(-1);
            presetManager_->refreshSlotOccupancy();
            presetManager_->invalidatePreloadCache();
            // Reload with factory defaults
            controlManager_->reloadConfig();
            loadSettings();
            loadingSlot_ = false;
            refreshUI();
            updateSlotButtonStates();
            pluginChainEditor_->refreshList();
        };
        settingsPanel->onSaveSettings = [this] {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Save Settings",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                    .getChildFile("DirectPipe_backup.dpbackup"),
                "*.dpbackup");
            auto safeThis = juce::Component::SafePointer<MainComponent>(this);
            chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                                 [safeThis, chooser](const juce::FileChooser& fc) {
                if (!safeThis) return;
                auto file = fc.getResult();
                if (file != juce::File()) {
                    auto target = file.withFileExtension("dpbackup");
                    auto json = SettingsExporter::exportAll(*safeThis->presetManager_, safeThis->controlManager_->getConfigStore());
                    target.replaceWithText(json);
                }
            });
        };
        settingsPanel->onLoadSettings = [this] {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Settings",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
                "*.dpbackup");
            auto safeThis = juce::Component::SafePointer<MainComponent>(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                                 [safeThis, chooser](const juce::FileChooser& fc) {
                if (!safeThis) return;
                auto file = fc.getResult();
                if (file.existsAsFile()) {
                    auto json = file.loadFileAsString();
                    safeThis->loadingSlot_ = true;
                    if (!SettingsExporter::importAll(json, *safeThis->presetManager_, safeThis->controlManager_->getConfigStore())) {
                        safeThis->loadingSlot_ = false;
                        juce::Logger::writeToLog("[APP] Failed to import settings from " + file.getFullPathName());
                        return;
                    }
                    safeThis->controlManager_->reloadConfig();
                    safeThis->loadingSlot_ = false;
                    safeThis->markSettingsDirty();
                    safeThis->refreshUI();
                    safeThis->updateSlotButtonStates();
                }
            });
        };
        settingsPanel->onFullBackup = [this] {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Full Backup",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
                    .getChildFile("DirectPipe_full.dpfullbackup"),
                "*.dpfullbackup");
            auto safeThis = juce::Component::SafePointer<MainComponent>(this);
            chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                                 [safeThis, chooser](const juce::FileChooser& fc) {
                if (!safeThis) return;
                auto file = fc.getResult();
                if (file != juce::File()) {
                    auto target = file.withFileExtension("dpfullbackup");
                    auto json = SettingsExporter::exportFullBackup(*safeThis->presetManager_, safeThis->controlManager_->getConfigStore());
                    target.replaceWithText(json);
                    juce::Logger::writeToLog("[APP] Full backup saved to " + target.getFullPathName());
                }
            });
        };
        settingsPanel->onFullRestore = [this] {
            auto chooser = std::make_shared<juce::FileChooser>(
                "Full Restore",
                juce::File::getSpecialLocation(juce::File::userDesktopDirectory),
                "*.dpfullbackup");
            auto safeThis = juce::Component::SafePointer<MainComponent>(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                                 [safeThis, chooser](const juce::FileChooser& fc) {
                if (!safeThis) return;
                auto file = fc.getResult();
                if (file.existsAsFile()) {
                    auto json = file.loadFileAsString();
                    safeThis->loadingSlot_ = true;
                    if (!SettingsExporter::importFullBackup(json, *safeThis->presetManager_, safeThis->controlManager_->getConfigStore())) {
                        safeThis->loadingSlot_ = false;
                        juce::Logger::writeToLog("[APP] Failed to import full backup from " + file.getFullPathName());
                        return;
                    }
                    safeThis->controlManager_->reloadConfig();
                    safeThis->presetManager_->refreshSlotOccupancy();
                    safeThis->presetManager_->loadSlotNames();
                    safeThis->presetManager_->invalidatePreloadCache();
                    safeThis->loadingSlot_ = false;
                    safeThis->markSettingsDirty();
                    safeThis->refreshUI();
                    safeThis->updateSlotButtonStates();
                    safeThis->pluginChainEditor_->refreshList();
                    juce::Logger::writeToLog("[APP] Full backup restored");
                }
            });
        };
        rightTabs_->addTab("Settings", tabBg, settingsPanel.release(), true);
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
        if (loadingSlot_ || partialLoad_) return;
        int slot = presetManager_->getActiveSlot();
        if (slot >= 0)
            presetManager_->saveSlot(slot);
        markSettingsDirty();
    };

    // Auto-save when a plugin editor window is closed (captures parameter changes)
    audioEngine_.getVSTChain().onEditorClosed = [this] {
        if (loadingSlot_ || partialLoad_) return;
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
        auto safeThis = juce::Component::SafePointer<MainComponent>(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                             juce::FileBrowserComponent::canSelectFiles,
                             [safeThis, chooser](const juce::FileChooser& fc) {
            if (!safeThis) return;
            auto file = fc.getResult();
            if (file != juce::File()) {
                auto target = file.withFileExtension("dppreset");
                safeThis->presetManager_->savePreset(target);
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
        auto safeThis = juce::Component::SafePointer<MainComponent>(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                             [safeThis, chooser](const juce::FileChooser& fc) {
            if (!safeThis) return;
            auto file = fc.getResult();
            if (file.existsAsFile()) {
                safeThis->loadingSlot_ = true;
                safeThis->presetManager_->loadPreset(file);
                safeThis->loadingSlot_ = false;
                safeThis->markSettingsDirty();
                safeThis->refreshUI();
                safeThis->updateSlotButtonStates();
            }
        });
    };
    addAndMakeVisible(loadPresetBtn_);

    // ── Quick Preset Slot Buttons (A..E) ──
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto label = presetManager_->getSlotDisplayName(i);
        slotButtons_[static_cast<size_t>(i)] = std::make_unique<juce::TextButton>(label);
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        btn->setClickingTogglesState(false);
        btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A40));
        btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF7B6FFF));
        btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        btn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        btn->onClick = [this, i] { onSlotClicked(i); };
        addAndMakeVisible(*btn);
    }

    // Right-click on slot buttons → "Copy to..." menu
    for (int i = 0; i < kNumPresetSlots; ++i) {
        slotButtons_[static_cast<size_t>(i)]->setMouseClickGrabsKeyboardFocus(false);
        slotButtons_[static_cast<size_t>(i)]->addMouseListener(this, false);
    }

    // ── Mute Status Indicators (clickable) ──
    auto setupMuteBtn = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF4CAF50));
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(btn);
    };
    setupMuteBtn(outputMuteBtn_);
    setupMuteBtn(monitorMuteBtn_);
    setupMuteBtn(vstMuteBtn_);
    vstMuteBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
    outputMuteBtn_.onClick = [this] {
        if (audioEngine_.isMuted()) return;  // Locked during panic mute
        if (audioEngine_.isOutputNone()) return;  // Can't unmute when output is "None"
        bool outputMuted = !audioEngine_.isOutputMuted();
        audioEngine_.setOutputMuted(outputMuted);
        audioEngine_.clearOutputAutoMute();  // User manually toggled — take over from auto-mute
        markSettingsDirty();
    };
    monitorMuteBtn_.onClick = [this] {
        if (audioEngine_.isMuted()) return;  // Locked during panic mute
        auto& router = audioEngine_.getOutputRouter();
        bool enabled = !router.isEnabled(OutputRouter::Output::Monitor);
        router.setEnabled(OutputRouter::Output::Monitor, enabled);
        audioEngine_.setMonitorEnabled(enabled);
        markSettingsDirty();
    };
    vstMuteBtn_.onClick = [this] {
        if (audioEngine_.isMuted()) return;  // Locked during panic mute
        bool enabled = !audioEngine_.isIpcEnabled();
        audioEngine_.setIpcEnabled(enabled);
        // Sync Output tab toggle
        if (auto* outPanel = dynamic_cast<OutputPanel*>(rightTabs_->getTabContentComponent(1)))
            outPanel->setIpcToggleState(enabled);
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
            preMuteVstEnabled_ = audioEngine_.isIpcEnabled();
            audioEngine_.setOutputMuted(false);
            router.setEnabled(OutputRouter::Output::Monitor, false);
            audioEngine_.setMonitorEnabled(false);
            if (preMuteVstEnabled_) audioEngine_.setIpcEnabled(false);
        } else {
            // Respect outputNone_ — keep muted if output is "None"
            bool restoreMuted = audioEngine_.isOutputNone() ? true : preMuteOutputMuted_;
            audioEngine_.setOutputMuted(restoreMuted);
            router.setEnabled(OutputRouter::Output::Monitor, preMuteMonitorEnabled_);
            audioEngine_.setMonitorEnabled(preMuteMonitorEnabled_);
            if (preMuteVstEnabled_) audioEngine_.setIpcEnabled(true);
        }
        // Sync Output tab VST toggle
        if (auto* outPanel = dynamic_cast<OutputPanel*>(rightTabs_->getTabContentComponent(1)))
            outPanel->setIpcToggleState(audioEngine_.isIpcEnabled());
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
        label.setFont(juce::Font(13.0f));
        label.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible(label);
    };
    setupStatusLabel(latencyLabel_);
    setupStatusLabel(cpuLabel_);
    setupStatusLabel(formatLabel_);
    setupStatusLabel(portableLabel_);

    // Show portable mode indicator
    if (ControlMappingStore::isPortableMode()) {
        portableLabel_.setText("Portable", juce::dontSendNotification);
        portableLabel_.setColour(juce::Label::textColourId,
            enableExternalControls ? juce::Colour(0xFF6C63FF) : juce::Colour(0xFFFF9800));

        // Audio-only mode: append to window title for persistent visibility
        if (!enableExternalControls) {
            auto safeThis = juce::Component::SafePointer<MainComponent>(this);
            juce::MessageManager::callAsync([safeThis] {
                if (!safeThis) return;
                if (auto* w = safeThis->getTopLevelComponent())
                    if (auto* dw = dynamic_cast<juce::DocumentWindow*>(w))
                        dw->setName(dw->getName() + " (Audio Only)");
            });
        }
    }

    // Credit + version hyperlink (click opens GitHub)
    creditLink_.setButtonText("v" + juce::String(ProjectInfo::versionString) + " | Created by LiveTrack");
    creditLink_.setFont(juce::Font(12.0f), false);
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
        presetManager_->setActiveSlot(0);
    }
    updateSlotButtonStates();

    // Clean up leftover update files and check post-update flag
    {
        auto exeDir = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory();
        exeDir.getChildFile(kUpdateBatchFile).deleteFile();
        exeDir.getChildFile(kUpdateDir).deleteRecursively();
        exeDir.getChildFile(kUpdateZip).deleteFile();
        exeDir.getChildFile(kUpdateExe).deleteFile();
        exeDir.getChildFile(kBackupExe).deleteFile();

        auto flagFile = exeDir.getChildFile(kUpdatedFlag);
        if (flagFile.existsAsFile()) {
            auto version = flagFile.loadFileAsString().trim();
            auto safeThis = juce::Component::SafePointer<MainComponent>(this);
            juce::MessageManager::callAsync([safeThis, version, flagFile]() {
                if (!safeThis) return;
                safeThis->showNotification("Updated to v" + version + " successfully!",
                                 NotificationLevel::Info);
                flagFile.deleteFile();
            });
        }
    }

    // Check for new release on GitHub (background thread)
    checkForUpdate();

    // Notify if running in audio-only mode (external controls disabled)
    if (!controlManager_->isExternalControlsActive()) {
        auto safeThis = juce::Component::SafePointer<MainComponent>(this);
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis)
                safeThis->showNotification(
                    "Audio only mode (another DirectPipe controls hotkeys/MIDI/API)",
                    NotificationLevel::Info);
        });
    }
}

MainComponent::~MainComponent()
{
    stopTimer();
    // Remove slot button mouse listeners (right-click menu)
    for (int i = 0; i < kNumPresetSlots; ++i)
        slotButtons_[static_cast<size_t>(i)]->removeMouseListener(this);
    if (downloadThread_.joinable())
        downloadThread_.join();
    if (updateCheckThread_.joinable())
        updateCheckThread_.join();
    saveSettings();
    dispatcher_.removeListener(this);
    controlManager_->shutdown();
    audioEngine_.shutdown();
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
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
            audioEngine_.getVSTChain().togglePluginBypassed(event.intParam);
            break;
        }

        case Action::MasterBypass: {
            bool anyActive = false;
            for (int i = 0; i < audioEngine_.getVSTChain().getPluginCount(); ++i) {
                if (!audioEngine_.getVSTChain().isPluginBypassed(i)) { anyActive = true; break; }
            }
            // Suppress auto-save during batch bypass toggle (save once at end)
            loadingSlot_ = true;
            for (int i = 0; i < audioEngine_.getVSTChain().getPluginCount(); ++i)
                audioEngine_.getVSTChain().setPluginBypassed(i, anyActive);
            loadingSlot_ = false;
            // Save once after all bypass changes
            int activeSlot = presetManager_->getActiveSlot();
            if (activeSlot >= 0 && !partialLoad_.load())
                presetManager_->saveSlot(activeSlot);
            markSettingsDirty();
            break;
        }

        case Action::ToggleMute: {
            if (event.stringParam == "monitor") {
                if (audioEngine_.isMuted()) break;  // Locked during panic mute
                // Monitor mute = toggle monitor enable (headphones only)
                auto& router = audioEngine_.getOutputRouter();
                bool enabled = !router.isEnabled(OutputRouter::Output::Monitor);
                router.setEnabled(OutputRouter::Output::Monitor, enabled);
                audioEngine_.setMonitorEnabled(enabled);
            } else if (event.stringParam == "output") {
                if (audioEngine_.isMuted()) break;  // Locked during panic mute
                if (audioEngine_.isOutputNone()) break;  // Can't unmute when output is "None"
                // Output mute = silence main output only, monitor keeps working
                bool outputMuted = !audioEngine_.isOutputMuted();
                audioEngine_.setOutputMuted(outputMuted);
                audioEngine_.clearOutputAutoMute();  // User manually toggled
            } else {
                // Input / global mute (same as panic)
                bool muted = !audioEngine_.isMuted();
                audioEngine_.setMuted(muted);
                auto& router = audioEngine_.getOutputRouter();
                if (muted) {
                    preMuteMonitorEnabled_ = router.isEnabled(OutputRouter::Output::Monitor);
                    preMuteOutputMuted_ = audioEngine_.isOutputMuted();
                    preMuteVstEnabled_ = audioEngine_.isIpcEnabled();
                    audioEngine_.setOutputMuted(false);
                    router.setEnabled(OutputRouter::Output::Monitor, false);
                    audioEngine_.setMonitorEnabled(false);
                    if (preMuteVstEnabled_) audioEngine_.setIpcEnabled(false);
                } else {
                    bool restoreMuted = audioEngine_.isOutputNone() ? true : preMuteOutputMuted_;
                    audioEngine_.setOutputMuted(restoreMuted);
                    router.setEnabled(OutputRouter::Output::Monitor, preMuteMonitorEnabled_);
                    audioEngine_.setMonitorEnabled(preMuteMonitorEnabled_);
                    if (preMuteVstEnabled_) audioEngine_.setIpcEnabled(true);
                }
                if (auto* outPanel = dynamic_cast<OutputPanel*>(rightTabs_->getTabContentComponent(1)))
                    outPanel->setIpcToggleState(audioEngine_.isIpcEnabled());
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
                preMuteVstEnabled_ = audioEngine_.isIpcEnabled();
                audioEngine_.setOutputMuted(false);
                router.setEnabled(OutputRouter::Output::Monitor, false);
                audioEngine_.setMonitorEnabled(false);
                if (preMuteVstEnabled_) audioEngine_.setIpcEnabled(false);
            } else {
                bool restoreMuted = audioEngine_.isOutputNone() ? true : preMuteOutputMuted_;
                audioEngine_.setOutputMuted(restoreMuted);
                router.setEnabled(OutputRouter::Output::Monitor, preMuteMonitorEnabled_);
                audioEngine_.setMonitorEnabled(preMuteMonitorEnabled_);
                if (preMuteVstEnabled_) audioEngine_.setIpcEnabled(true);
            }
            if (auto* outPanel = dynamic_cast<OutputPanel*>(rightTabs_->getTabContentComponent(1)))
                outPanel->setIpcToggleState(audioEngine_.isIpcEnabled());
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
            if (event.stringParam == "monitor") {
                if (audioEngine_.isMuted()) break;  // Locked during panic mute
                audioEngine_.getOutputRouter().setVolume(OutputRouter::Output::Monitor, event.floatParam);
                markSettingsDirty();
            } else if (event.stringParam == "input") {
                audioEngine_.setInputGain(event.floatParam);
                inputGainSlider_.setValue(event.floatParam, juce::dontSendNotification);
                markSettingsDirty();
            }
            break;

        case Action::MonitorToggle: {
            if (audioEngine_.isMuted()) break;  // Locked during panic mute
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
                if (!recorder.startRecording(file, sr, 2))
                    showNotification("Recording failed - check folder permissions",
                                     NotificationLevel::Error);
            }
            break;
        }

        case Action::SetPluginParameter: {
            audioEngine_.getVSTChain().setPluginParameter(
                event.intParam, event.intParam2, event.floatParam);
            break;
        }

        case Action::IpcToggle: {
            if (audioEngine_.isMuted()) break;  // Locked during panic mute
            bool enabled = !audioEngine_.isIpcEnabled();
            audioEngine_.setIpcEnabled(enabled);
            // Sync Output tab toggle
            if (auto* outPanel = dynamic_cast<OutputPanel*>(rightTabs_->getTabContentComponent(1)))
                outPanel->setIpcToggleState(enabled);
            markSettingsDirty();
            break;
        }

        case Action::LoadPreset:
        case Action::SwitchPresetSlot: {
            int slot = event.intParam;
            if (slot >= 0 && slot < kNumPresetSlots) {
                if (loadingSlot_) {
                    pendingSlot_ = slot;  // queue latest, process after current load
                } else {
                    onSlotClicked(slot);
                }
            }
            break;
        }

        case Action::NextPreset:
        case Action::PreviousPreset: {
            int currentSlot = presetManager_->getActiveSlot();
            int direction = (event.action == Action::NextPreset) ? 1 : -1;
            int start = (currentSlot < 0) ? 0 : currentSlot;
            int nextSlot = (start + direction + kNumPresetSlots) % kNumPresetSlots;
            if (loadingSlot_) {
                pendingSlot_ = nextSlot;
            } else {
                onSlotClicked(nextSlot);
            }
            break;
        }

        default:
            break;
    }
}

// ─── Preset Slots ───────────────────────────────────────────────────────────

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    // Right-click on slot buttons → context menu
    if (!event.mods.isPopupMenu()) return;
    if (loadingSlot_) return;  // Prevent context menu during async slot load

    auto* src = event.eventComponent;
    int sourceSlot = -1;
    for (int i = 0; i < kNumPresetSlots; ++i) {
        if (src == slotButtons_[static_cast<size_t>(i)].get()) {
            sourceSlot = i;
            break;
        }
    }
    if (sourceSlot < 0) return;

    bool occupied = presetManager_->isSlotOccupied(sourceSlot);
    auto slotChar = juce::String::charToString(PresetManager::slotLabel(sourceSlot));

    juce::PopupMenu menu;

    // Rename (always available for occupied slots)
    if (occupied)
        menu.addItem(200, "Rename " + slotChar + "...");

    // Copy submenu (occupied only)
    if (occupied) {
        juce::PopupMenu copyMenu;
        for (int i = 0; i < kNumPresetSlots; ++i) {
            if (i == sourceSlot) continue;
            copyMenu.addItem(i + 1, "-> " + juce::String::charToString(PresetManager::slotLabel(i)));
        }
        menu.addSubMenu("Copy " + slotChar, copyMenu);
    }

    // Export/Import
    if (occupied)
        menu.addItem(300, "Export " + slotChar + "...");
    menu.addItem(301, "Import to " + slotChar + "...");

    // Delete (occupied only)
    if (occupied) {
        menu.addSeparator();
        menu.addItem(100, "Delete " + slotChar);
    }

    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    menu.showMenuAsync(juce::PopupMenu::Options(),
        [safeThis, sourceSlot, slotChar](int result) {
            if (!safeThis || result == 0) return;

            if (result == 200) {
                // Rename
                auto currentName = safeThis->presetManager_->getSlotName(sourceSlot);
                auto* aw = new juce::AlertWindow("Rename Slot " + slotChar,
                    "Enter a name for slot " + slotChar + ":",
                    juce::MessageBoxIconType::NoIcon);
                aw->addTextEditor("name", currentName, "Name:");
                aw->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
                aw->addButton("Clear", 2);
                aw->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));

                auto safeThis2 = safeThis;
                aw->enterModalState(true, juce::ModalCallbackFunction::create(
                    [safeThis2, sourceSlot, aw](int btnResult) {
                        if (!safeThis2) { delete aw; return; }
                        if (btnResult == 1) {
                            auto newName = aw->getTextEditorContents("name");
                            safeThis2->presetManager_->setSlotName(sourceSlot, newName);
                        } else if (btnResult == 2) {
                            safeThis2->presetManager_->setSlotName(sourceSlot, "");
                        }
                        delete aw;
                        if (btnResult > 0) {
                            safeThis2->updateSlotButtonStates();
                            safeThis2->markSettingsDirty();
                        }
                    }), true);
                return;
            }

            if (result == 300) {
                // Export
                safeThis->presetManager_->exportSlot(sourceSlot);
                return;
            }

            if (result == 301) {
                // Import
                auto safeThis2 = safeThis;
                safeThis->presetManager_->importSlot(sourceSlot, [safeThis2](bool ok) {
                    if (!safeThis2) return;
                    safeThis2->updateSlotButtonStates();
                    safeThis2->refreshUI();
                    if (ok) safeThis2->markSettingsDirty();
                });
                return;
            }

            if (result == 100) {
                // Delete
                bool wasActive = (safeThis->presetManager_->getActiveSlot() == sourceSlot);
                if (safeThis->presetManager_->deleteSlot(sourceSlot)) {
                    if (wasActive) {
                        safeThis->loadingSlot_ = true;
                        auto& chain = safeThis->audioEngine_.getVSTChain();
                        while (chain.getPluginCount() > 0)
                            chain.removePlugin(0);
                        safeThis->loadingSlot_ = false;
                        // Keep deleted slot selected (now empty)
                        safeThis->presetManager_->setActiveSlot(sourceSlot);
                        if (safeThis->pluginChainEditor_)
                            safeThis->pluginChainEditor_->refreshList();
                    }
                    safeThis->updateSlotButtonStates();
                    safeThis->showNotification("Deleted slot " + slotChar,
                                               NotificationLevel::Info);
                    safeThis->markSettingsDirty();
                }
                return;
            }

            // Copy (result 1-4 = target slot index)
            int targetSlot = result - 1;
            int active = safeThis->presetManager_->getActiveSlot();
            if (active == sourceSlot && !safeThis->partialLoad_.load())
                safeThis->presetManager_->saveSlot(sourceSlot);

            if (safeThis->presetManager_->copySlot(sourceSlot, targetSlot)) {
                safeThis->updateSlotButtonStates();
                safeThis->showNotification(
                    "Copied slot " + slotChar
                    + " to " + juce::String::charToString(PresetManager::slotLabel(targetSlot)),
                    NotificationLevel::Info);

                // If copied TO the active slot, reload chain to reflect new content
                if (safeThis->presetManager_->getActiveSlot() == targetSlot) {
                    safeThis->loadingSlot_ = true;
                    safeThis->pluginChainEditor_->showLoadingState();
                    safeThis->presetManager_->loadSlotAsync(targetSlot,
                        [safeThis](bool ok) {
                            if (!safeThis) return;
                            safeThis->loadingSlot_ = false;
                            safeThis->partialLoad_ = !ok;
                            safeThis->pluginChainEditor_->hideLoadingState();
                            safeThis->refreshUI();
                            safeThis->updateSlotButtonStates();
                            if (ok) safeThis->markSettingsDirty();
                        });
                }
            }
        });
}

void MainComponent::onSlotClicked(int slotIndex)
{
    if (loadingSlot_) return;  // Prevent double-click during load

    // Same slot click = just save current state
    if (presetManager_->getActiveSlot() == slotIndex) {
        if (!partialLoad_.load())
            presetManager_->saveSlot(slotIndex);
        updateSlotButtonStates();
        return;
    }

    // Save current slot first (captures plugin internal state)
    // Skip save if previous load was partial (preserve original slot file)
    int currentSlot = presetManager_->getActiveSlot();
    if (currentSlot >= 0 && !partialLoad_.load())
        presetManager_->saveSlot(currentSlot);
    partialLoad_ = false;
    settingsDirty_ = false;
    dirtyCooldown_ = 0;

    // Slot has data → async load
    if (presetManager_->isSlotOccupied(slotIndex)) {
        loadingSlot_ = true;
        setSlotButtonsEnabled(false);
        pluginChainEditor_->showLoadingState();
        presetManager_->loadSlotAsync(slotIndex, [safeThis = juce::Component::SafePointer<MainComponent>(this)](bool ok) {
            if (!safeThis) return;
            safeThis->loadingSlot_ = false;
            safeThis->partialLoad_ = !ok;  // protect slot file from overwrite on partial load
            safeThis->setSlotButtonsEnabled(true);
            safeThis->pluginChainEditor_->hideLoadingState();
            safeThis->refreshUI();
            safeThis->updateSlotButtonStates();
            if (ok) safeThis->markSettingsDirty();

            // Process queued slot request (from rapid switching)
            if (safeThis->pendingSlot_ >= 0) {
                int pending = safeThis->pendingSlot_;
                safeThis->pendingSlot_ = -1;
                safeThis->onSlotClicked(pending);
            }
        });
    } else {
        // Empty slot → clear chain, select the empty slot
        // Set loadingSlot_ to suppress onChainModified auto-save during the clear.
        // Without this, each removePlugin fires onChainChanged → onChainModified →
        // saveSlot(oldActiveSlot) with a partially-cleared chain, corrupting the old slot file.
        loadingSlot_ = true;
        auto& chain = audioEngine_.getVSTChain();
        while (chain.getPluginCount() > 0)
            chain.removePlugin(chain.getPluginCount() - 1);
        presetManager_->setActiveSlot(slotIndex);
        loadingSlot_ = false;
        markSettingsDirty();
    }

    // Immediately show target slot as selected
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        bool isTarget = (i == slotIndex);
        btn->setToggleState(isTarget, juce::dontSendNotification);
        btn->setColour(juce::TextButton::buttonColourId,
                       juce::Colour(isTarget ? 0xFF7B6FFF : 0xFF2A2A40));
        btn->setColour(juce::TextButton::buttonOnColourId,
                       juce::Colour(isTarget ? 0xFF7B6FFF : 0xFF2A2A40));
    }
}

void MainComponent::updateSlotButtonStates()
{
    int active = presetManager_->getActiveSlot();
    for (int i = 0; i < kNumPresetSlots; ++i) {
        auto* btn = slotButtons_[static_cast<size_t>(i)].get();
        bool isActive = (i == active);

        btn->setButtonText(presetManager_->getSlotDisplayName(i));
        btn->setToggleState(isActive, juce::dontSendNotification);

        // Set ALL 4 colour IDs to prevent stale colours
        // Non-active slots all share same appearance (no occupied vs empty distinction)
        if (isActive) {
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF7B6FFF));
            btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF7B6FFF));
            btn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
        } else {
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A40));
            btn->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xFF2A2A40));
            btn->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            btn->setColour(juce::TextButton::textColourOnId, juce::Colours::white);
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

    // Bottom controls: OUT/MON/VST indicators (24px) + gap (4px) + PANIC MUTE (28px) = 56px
    int bottomControlsH = 24 + 4 + 28;
    int bottomY = bounds.getBottom() - kStatusBarHeight - bottomControlsH;

    // Plugin chain fills remaining space between preset slots and bottom controls
    int chainH = bottomY - y - 4;
    pluginChainEditor_->setBounds(cx, y, cw, chainH);

    // Mute status indicators (OUT / MON / VST)
    {
        int gap = 4;
        int indicatorW = (cw - gap * 2) / 3;
        int lastW = cw - (indicatorW + gap) * 2;
        outputMuteBtn_.setBounds(cx, bottomY, indicatorW, 24);
        monitorMuteBtn_.setBounds(cx + indicatorW + gap, bottomY, indicatorW, 24);
        vstMuteBtn_.setBounds(cx + (indicatorW + gap) * 2, bottomY, lastW, 24);
    }

    // PANIC MUTE button (below indicators: 24px height + 4px gap)
    panicMuteBtn_.setBounds(cx, bottomY + 24 + 4, cw, 28);

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
    int creditW = 200;
    int infoW = getWidth() - creditW - 10;  // all space minus credit area
    int col3 = infoW / 3;
    latencyLabel_.setBounds(5, statusY, col3, 24);
    cpuLabel_.setBounds(5 + col3, statusY, col3, 24);
    formatLabel_.setBounds(5 + col3 * 2, statusY, col3, 24);
    portableLabel_.setBounds(5 + infoW, statusY, 100, 24);
    creditLink_.setBounds(getWidth() - creditW, statusY, creditW - 5, 24);
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
        if (notifActive != cachedNotifActive_) {
            cachedNotifActive_ = notifActive;
            latencyLabel_.setVisible(!notifActive);
            cpuLabel_.setVisible(!notifActive);
            formatLabel_.setVisible(!notifActive);
            portableLabel_.setVisible(!notifActive);
        }
    }

    // ── Flush log entries to Log tab ──
    if (auto* logComp = dynamic_cast<LogPanel*>(rightTabs_->getTabContentComponent(3)))
        logComp->flushPendingLogs();

    auto& monitor = audioEngine_.getLatencyMonitor();

    bool muted = audioEngine_.isMuted();
    inputMeter_->setLevel(audioEngine_.getInputLevel());
    outputMeter_->setLevel(muted ? 0.0f : audioEngine_.getOutputLevel());
    inputMeter_->tick();
    outputMeter_->tick();

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

        bool vstActive = audioEngine_.isIpcEnabled();
        if (vstActive != cachedVstEnabled_) {
            cachedVstEnabled_ = vstActive;
            vstMuteBtn_.setColour(juce::TextButton::buttonColourId,
                vstActive ? juce::Colour(0xFF4CAF50) : juce::Colour(0xFFE05050));
        }
    }

    // Main output latency: input buffer + processing + output buffer
    double mainLatency = monitor.getTotalLatencyVirtualMicMs();

    // Monitor output latency
    auto& monOut = audioEngine_.getMonitorOutput();
    auto& router = audioEngine_.getOutputRouter();
    bool monEnabled = router.isEnabled(OutputRouter::Output::Monitor);

    // Latency label — only rebuild string when values change
    {
        double monitorLatency = 0.0;
        if (monEnabled) {
            monitorLatency = mainLatency;
            if (monOut.isActive()) {
                double monSR = monOut.getActualSampleRate();
                if (monSR > 0.0)
                    monitorLatency += (static_cast<double>(monOut.getActualBufferSize()) / monSR) * 1000.0;
            }
        }
        // Check if values changed (0.05ms precision)
        if (std::abs(mainLatency - cachedMainLatency_) > 0.05 ||
            std::abs(monitorLatency - cachedMonitorLatency_) > 0.05 ||
            monEnabled != cachedMonEnabled_)
        {
            cachedMainLatency_ = mainLatency;
            cachedMonitorLatency_ = monitorLatency;
            cachedMonEnabled_ = monEnabled;
            juce::String latencyText = "Latency: " + juce::String(mainLatency, 1) + "ms";
            if (monEnabled)
                latencyText += " | Mon: " + juce::String(monitorLatency, 1) + "ms";
            latencyLabel_.setText(latencyText, juce::dontSendNotification);
        }
    }

    // CPU/XRun label — only rebuild when values change
    {
        audioEngine_.updateXRunTracking();
        double cpuPct = monitor.getCpuUsagePercent();
        int xruns = audioEngine_.getRecentXRunCount();
        if (std::abs(cpuPct - cachedCpuPercent_) > 0.1 || xruns != cachedXruns_) {
            cachedCpuPercent_ = cpuPct;
            cachedXruns_ = xruns;
            juce::String cpuText = "CPU: " + juce::String(cpuPct, 1) + "%";
            if (xruns > 0) {
                cpuText += " | XRun: " + juce::String(xruns);
                cpuLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFF6B6B));
            } else {
                cpuLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF8888AA));
            }
            cpuLabel_.setText(cpuText, juce::dontSendNotification);
        }
    }

    // Format label — SR/BS/channel rarely change, only rebuild on change
    {
        int sr = static_cast<int>(monitor.getSampleRate());
        int bs = monitor.getBufferSize();
        int cm = audioEngine_.getChannelMode();
        if (sr != cachedSampleRate_ || bs != cachedBufferSize_ || cm != cachedChannelMode_) {
            cachedSampleRate_ = sr;
            cachedBufferSize_ = bs;
            cachedChannelMode_ = cm;
            formatLabel_.setText(
                juce::String(sr) + "Hz / " + juce::String(bs) + " smp / " +
                juce::String(cm == 1 ? "Mono" : "Stereo"),
                juce::dontSendNotification);
        }
    }

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
        s.masterBypassed = true;
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
        s.ipcEnabled = audioEngine_.isIpcEnabled();
        s.deviceLost = audioEngine_.isDeviceLost();
        s.monitorLost = audioEngine_.getMonitorOutput().isDeviceLost();

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
        if (s.plugins.empty()) s.masterBypassed = false;

        // Slot names
        if (presetManager_) {
            for (int si = 0; si < kNumPresetSlots; ++si)
                s.slotNames[static_cast<size_t>(si)] = presetManager_->getSlotName(si).toStdString();
        }
    });

    // Update recording state in OutputPanel (Monitor tab)
    if (outputPanelPtr_) {
        bool isRec = audioEngine_.getRecorder().isRecording();
        double recSecs = audioEngine_.getRecorder().getRecordedSeconds();
        outputPanelPtr_->updateRecordingState(isRec, recSecs);
    }

    // Device reconnection check (~3s interval fallback for missed change notifications)
    audioEngine_.checkReconnection();

    // Dirty-flag auto-save with 1-second debounce (30 ticks at 30Hz)
    if (settingsDirty_ && dirtyCooldown_ > 0) {
        if (--dirtyCooldown_ == 0) {
            // Defer save if chain is in transitional state (async loading)
            if (loadingSlot_.load() || audioEngine_.getVSTChain().isLoading()) {
                dirtyCooldown_ = 10;  // retry in ~300ms
            } else {
                settingsDirty_ = false;
                saveSettings();
            }
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
    // Skip saving during async chain load — chain is in transitional state
    // (empty or partially loaded). Saving now would corrupt the active slot file.
    if (loadingSlot_.load() || audioEngine_.getVSTChain().isLoading())
        return;

    // Save current slot's chain state (captures plugin internal state)
    // Skip slot save if partial load (some plugins failed) — preserve original slot file
    int currentSlot = presetManager_->getActiveSlot();
    if (currentSlot >= 0 && !partialLoad_.load())
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

        // Self-healing: if settings.dppreset had an empty/corrupt chain but the
        // active slot file is valid, reload chain from the slot file.
        int activeSlot = presetManager_->getActiveSlot();
        if (activeSlot >= 0 && presetManager_->isSlotOccupied(activeSlot)
            && audioEngine_.getVSTChain().getPluginCount() == 0) {
            juce::Logger::writeToLog("[PRESET] Self-healing: reloading slot "
                + juce::String::charToString(PresetManager::slotLabel(activeSlot)) + " from file");
            presetManager_->loadSlot(activeSlot);
        }

        loadingSlot_ = false;

        // Restore panic mute lockout (monitor/IPC disabled while muted)
        if (audioEngine_.isMuted()) {
            auto& router = audioEngine_.getOutputRouter();
            preMuteMonitorEnabled_ = router.isEnabled(OutputRouter::Output::Monitor);
            preMuteOutputMuted_ = audioEngine_.isOutputMuted();
            preMuteVstEnabled_ = audioEngine_.isIpcEnabled();
            audioEngine_.setOutputMuted(false);
            router.setEnabled(OutputRouter::Output::Monitor, false);
            audioEngine_.setMonitorEnabled(false);
            if (preMuteVstEnabled_) audioEngine_.setIpcEnabled(false);
        }

        refreshUI();
        updateSlotButtonStates();

        // Sync Settings tab UI (audit mode toggle, pending logs) before window is shown
        if (auto* settingsPanel = dynamic_cast<LogPanel*>(rightTabs_->getTabContentComponent(3)))
            settingsPanel->flushPendingLogs();

        // Pre-load other slots in background for instant switching.
        // Deferred via callAsync to ensure audio device is fully started
        // before preload thread uses formatManager.
        // Window is shown AFTER preload completes (prevents "frozen UI" appearance).
        auto safeThis = juce::Component::SafePointer<MainComponent>(this);
        juce::MessageManager::callAsync([safeThis]() {
            if (!safeThis) return;
            safeThis->presetManager_->triggerPreload([safeThis]() {
                if (!safeThis) return;
                if (auto* w = safeThis->getTopLevelComponent())
                    w->setVisible(true);
            });
        });
        return;  // window will be shown by preload callback
    }

    // No settings file → show window immediately
    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (!safeThis) return;
        if (auto* w = safeThis->getTopLevelComponent())
            w->setVisible(true);
    });
}

void MainComponent::refreshUI()
{
    inputGainSlider_.setValue(audioEngine_.getInputGain(), juce::dontSendNotification);

    // Get tab content components for refresh
    if (auto* audioSettingsComp = dynamic_cast<AudioSettings*>(rightTabs_->getTabContentComponent(0))) {
        audioSettingsComp->refreshFromEngine();
    }

    if (pluginChainEditor_)
        pluginChainEditor_->refreshList();

    if (auto* outputComp = dynamic_cast<OutputPanel*>(rightTabs_->getTabContentComponent(1)))
        outputComp->refreshDeviceLists();

    bool muted = audioEngine_.isMuted();
    panicMuteBtn_.setButtonText(muted ? "UNMUTE" : "PANIC MUTE");
    panicMuteBtn_.setColour(juce::TextButton::buttonColourId,
                            juce::Colour(muted ? 0xFF4CAF50u : 0xFFE05050u));

    // IPC toggle state (Output tab = index 1)
    if (auto* outPanel = dynamic_cast<OutputPanel*>(rightTabs_->getTabContentComponent(1)))
        outPanel->setIpcToggleState(audioEngine_.isIpcEnabled());
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
    auto currentVersion = juce::String(ProjectInfo::versionString);
    auto safeThis = juce::Component::SafePointer<MainComponent>(this);

    updateCheckThread_ = std::thread([safeThis, currentVersion]() {
        juce::URL url("https://api.github.com/repos/LiveTrack-X/DirectPipe/releases/latest");
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(10000));
        juce::String response;
        if (stream) response = stream->readEntireStreamAsString();
        if (response.isEmpty()) return;

        auto parsed = juce::JSON::parse(response);
        if (auto* obj = parsed.getDynamicObject()) {
            auto tagName = obj->getProperty("tag_name").toString();
            if (tagName.startsWithChar('v') || tagName.startsWithChar('V'))
                tagName = tagName.substring(1);

            if (tagName.isNotEmpty() && tagName != currentVersion) {
                auto parseVer = [](const juce::String& v) -> std::tuple<int,int,int> {
                    auto parts = juce::StringArray::fromTokens(v, ".", "");
                    return { parts.size() > 0 ? parts[0].getIntValue() : 0,
                             parts.size() > 1 ? parts[1].getIntValue() : 0,
                             parts.size() > 2 ? parts[2].getIntValue() : 0 };
                };
                auto [rMaj, rMin, rPat] = parseVer(tagName);
                auto [cMaj, cMin, cPat] = parseVer(currentVersion);

                if (std::tie(rMaj, rMin, rPat) > std::tie(cMaj, cMin, cPat)) {
                    // Find download URL from assets (prefer ZIP, fallback to exe)
                    juce::String downloadUrl;
                    if (auto* assets = obj->getProperty("assets").getArray()) {
                        juce::String exeUrl;
                        for (auto& asset : *assets) {
                            if (auto* assetObj = asset.getDynamicObject()) {
                                auto name = assetObj->getProperty("name").toString();
                                auto assetUrl = assetObj->getProperty("browser_download_url").toString();
                                // Prefer ZIP (contains exe + VST + docs)
                                if (name.endsWithIgnoreCase(".zip") &&
                                    name.containsIgnoreCase("DirectPipe")) {
                                    downloadUrl = assetUrl;
                                    break;
                                }
                                // Fallback: standalone exe
                                if (name.endsWithIgnoreCase(".exe") &&
                                    name.containsIgnoreCase("DirectPipe")) {
                                    exeUrl = assetUrl;
                                }
                            }
                        }
                        if (downloadUrl.isEmpty())
                            downloadUrl = exeUrl;
                    }

                    juce::MessageManager::callAsync([safeThis, tagName, downloadUrl]() {
                        if (auto* self = safeThis.getComponent()) {
                            self->latestVersion_ = tagName;
                            self->latestDownloadUrl_ = downloadUrl;
                            self->updateAvailable_ = true;

                            self->creditLink_.setButtonText(
                                "NEW v" + tagName + " | v" +
                                juce::String(ProjectInfo::versionString) + " | Created by LiveTrack");
                            self->creditLink_.setColour(juce::HyperlinkButton::textColourId,
                                                        juce::Colour(0xFFFFAA33));

                            // Override click to show update dialog instead of opening browser
                            self->creditLink_.setURL({});
                            self->creditLink_.onClick = [safeThis]() {
                                if (safeThis) safeThis->showUpdateDialog();
                            };
                        }
                    });
                }
            }
        }
    });
}

void MainComponent::showUpdateDialog()
{
    auto* window = new juce::AlertWindow(
        "Update Available",
        "New version v" + latestVersion_ + " is available.\n"
        "Current version: v" + juce::String(ProjectInfo::versionString) + "\n\n"
        "Would you like to update?",
        juce::MessageBoxIconType::InfoIcon);

    window->addButton("Update Now", 1);
    window->addButton("View on GitHub", 2);
    window->addButton("Later", 0);

    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [safeThis](int result) {
            if (!safeThis) return;
            if (result == 1) {
                safeThis->performUpdate();
            } else if (result == 2) {
                juce::URL("https://github.com/LiveTrack-X/DirectPipe/releases/latest")
                    .launchInDefaultBrowser();
            }
        }), true);
}

void MainComponent::performUpdate()
{
    if (latestDownloadUrl_.isEmpty()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update Error",
            "Download URL not found.\nPlease download manually from GitHub.",
            "OK");
        juce::URL("https://github.com/LiveTrack-X/DirectPipe/releases/latest")
            .launchInDefaultBrowser();
        return;
    }

    // Determine paths
    auto currentExe = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    auto updateDir = currentExe.getParentDirectory().getChildFile(kUpdateDir);
    auto batchFile = currentExe.getSiblingFile(kUpdateBatchFile);

    // Show progress (indeterminate spinner)
    auto progressDlg = std::make_shared<std::unique_ptr<juce::AlertWindow>>(
        std::make_unique<juce::AlertWindow>("Updating...",
            "Downloading v" + latestVersion_ + "...",
            juce::MessageBoxIconType::NoIcon));
    downloadProgress_ = -1.0;
    (*progressDlg)->addProgressBarComponent(downloadProgress_);
    (*progressDlg)->enterModalState(true, nullptr, false);

    auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    auto downloadUrl = latestDownloadUrl_;
    auto version = latestVersion_;
    bool isZip = downloadUrl.endsWithIgnoreCase(".zip");

    // If a previous download is still running, reject the request (don't block message thread)
    if (downloadThread_.joinable()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Update in Progress",
            "A download is already in progress. Please wait for it to finish.",
            "OK");
        (*progressDlg)->exitModalState(0);
        return;
    }
    downloadThread_ = std::thread([safeThis, downloadUrl, updateDir, batchFile, currentExe, version, isZip, progressDlg]() {
        // Download the file
        juce::URL url(downloadUrl);
        int statusCode = 0;
        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(15000)
                .withStatusCode(&statusCode));

        if (!stream || statusCode != 200) {
            juce::MessageManager::callAsync([safeThis, progressDlg]() {
                if (safeThis) {
                    if (*progressDlg)
                        (*progressDlg)->exitModalState(0);
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Download Failed",
                        "Could not download the update.\nPlease try again or download manually.",
                        "OK");
                }
            });
            return;
        }

        // Determine download target
        auto downloadFile = isZip
            ? currentExe.getSiblingFile(kUpdateZip)
            : currentExe.getSiblingFile(kUpdateExe);

        // Write to file
        {
            juce::FileOutputStream output(downloadFile);
            if (!output.openedOk()) {
                juce::MessageManager::callAsync([safeThis, progressDlg]() {
                    if (safeThis) {
                        if (*progressDlg)
                            (*progressDlg)->exitModalState(0);
                        juce::AlertWindow::showMessageBoxAsync(
                            juce::MessageBoxIconType::WarningIcon,
                            "Update Error",
                            "Could not write update file.\nCheck write permissions.",
                            "OK");
                    }
                });
                return;
            }

            char buffer[8192];
            while (!stream->isExhausted()) {
                auto bytesRead = stream->read(buffer, sizeof(buffer));
                if (bytesRead <= 0) break;
                output.write(buffer, static_cast<size_t>(bytesRead));
            }
            output.flush();
        }

        // Verify downloaded file
        if (!downloadFile.existsAsFile() || downloadFile.getSize() < 100 * 1024) {
            downloadFile.deleteFile();
            juce::MessageManager::callAsync([safeThis, progressDlg]() {
                if (safeThis) {
                    if (*progressDlg)
                        (*progressDlg)->exitModalState(0);
                    juce::AlertWindow::showMessageBoxAsync(
                        juce::MessageBoxIconType::WarningIcon,
                        "Download Failed",
                        "Downloaded file appears invalid.\nPlease download manually.",
                        "OK");
                }
            });
            return;
        }

        // Create update batch script
        auto exeDir = currentExe.getParentDirectory().getFullPathName().replace("/", "\\");
        auto currentPath = currentExe.getFullPathName().replace("/", "\\");
        auto downloadPath = downloadFile.getFullPathName().replace("/", "\\");
        auto backupPath = currentExe.getSiblingFile(kBackupExe)
                              .getFullPathName().replace("/", "\\");
        auto batchPath = batchFile.getFullPathName().replace("/", "\\");
        auto updateDirPath = updateDir.getFullPathName().replace("/", "\\");

        juce::String script;
        script << "@echo off\r\n";
        script << "chcp 65001 > nul\r\n";
        script << "echo.\r\n";
        script << "echo  Updating DirectPipe to v" << version << " ...\r\n";
        script << "echo  Waiting for DirectPipe to close...\r\n";
        script << "timeout /t 3 /nobreak > nul\r\n";
        script << ":waitloop\r\n";
        script << "tasklist /FI \"IMAGENAME eq DirectPipe.exe\" 2>nul | find /I \"DirectPipe.exe\" > nul\r\n";
        script << "if not errorlevel 1 (\r\n";
        script << "    timeout /t 1 /nobreak > nul\r\n";
        script << "    goto waitloop\r\n";
        script << ")\r\n";

        auto flagPath = currentExe.getSiblingFile(kUpdatedFlag)
                            .getFullPathName().replace("/", "\\");

        if (isZip) {
            // Extract ZIP using PowerShell, then copy DirectPipe.exe
            script << "echo  Extracting update...\r\n";
            script << "if exist \"" << updateDirPath << "\" rd /s /q \"" << updateDirPath << "\"\r\n";
            script << "powershell -NoProfile -Command \"Expand-Archive -Path '" << downloadPath
                   << "' -DestinationPath '" << updateDirPath << "' -Force\"\r\n";
            // Backup current exe
            script << "if exist \"" << backupPath << "\" del /f \"" << backupPath << "\"\r\n";
            script << "move /y \"" << currentPath << "\" \"" << backupPath << "\"\r\n";
            // Find and copy DirectPipe.exe from extracted folder (may be in subfolder)
            // Use PowerShell for reliable recursive file search (avoids cmd for/goto issues)
            script << "powershell -NoProfile -Command \"$f = Get-ChildItem -Path '" << updateDirPath
                   << "' -Recurse -Filter 'DirectPipe.exe' | Select-Object -First 1; "
                   << "if ($f) { Copy-Item $f.FullName -Destination '" << currentPath << "' -Force }\"\r\n";
            // Clean up: remove extracted folder, zip, and backup
            script << "rd /s /q \"" << updateDirPath << "\"\r\n";
            script << "del /f \"" << downloadPath << "\"\r\n";
            script << "del /f \"" << backupPath << "\"\r\n";
        } else {
            // Simple exe replacement
            script << "echo  Applying update...\r\n";
            script << "if exist \"" << backupPath << "\" del /f \"" << backupPath << "\"\r\n";
            script << "move /y \"" << currentPath << "\" \"" << backupPath << "\"\r\n";
            script << "move /y \"" << downloadPath << "\" \"" << currentPath << "\"\r\n";
            script << "del /f \"" << backupPath << "\"\r\n";
        }

        // Write flag file so app shows "Update Complete" on next launch
        script << "echo " << version << " > \"" << flagPath << "\"\r\n";
        // Launch new exe as fully detached process (not child of cmd)
        script << "powershell -NoProfile -Command \"Start-Process -FilePath '" << currentPath << "'\"\r\n";
        script << "exit\r\n";

        batchFile.replaceWithText(script);

        juce::MessageManager::callAsync([safeThis, batchFile, progressDlg]() {
            if (!safeThis) return;

            // Dismiss progress dialog
            if (*progressDlg)
                (*progressDlg)->exitModalState(0);

            // Launch batch script and quit
            batchFile.startAsProcess();
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        });
    });
}

} // namespace directpipe
