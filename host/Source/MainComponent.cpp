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
    controlManager_ = std::make_unique<ControlManager>(dispatcher_, broadcaster_, audioEngine_);
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
    audioSettings_->onError = [this](const juce::String& msg) {
        showNotification(msg, NotificationLevel::Error);
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

    // ── Auto Button (switches to Auto preset slot, index 5) ──
    //
    // ## Auto Slot Architecture
    //
    // Auto is a SPECIAL preset slot (index 5, defined as PresetSlotBar::kAutoSlotIndex).
    // It is NOT part of the A-E preset bar (indices 0-4). Key differences:
    //
    // 1. First click: If no Auto preset file exists yet, creates a DEFAULT chain
    //    with 3 built-in processors (Filter + Noise Removal + Auto Gain) and saves it.
    //    This provides instant "one-click audio improvement" for new users.
    //
    // 2. Subsequent clicks: Loads the saved Auto preset (which may have been customized
    //    by the user, e.g., different NR strength or AGC target).
    //
    // 3. Right-click: Shows a context menu with "Reset Auto to Defaults" option
    //    that recreates the default 3-processor chain (see mouseDown handler below).
    //
    // 4. Auto slot is EXCLUDED from Next/Previous preset cycling (A→B→C→D→E→A).
    //    External controls (hotkeys, MIDI, Stream Deck) cycle only through A-E.
    //    Auto must be explicitly selected.
    //
    // 5. When Auto is active, the A-E buttons are visually deselected and the
    //    Auto button turns green (0xFF4CAF50). See updateAutoButtonVisual().
    autoProcessorBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A4035));
    autoProcessorBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    autoProcessorBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    autoProcessorBtn_.onClick = [this] {
        if (loadingSlot_) return;

        int autoIdx = PresetSlotBar::kAutoSlotIndex;

        // Already on Auto slot — just save current state
        if (presetManager_ && presetManager_->getActiveSlot() == autoIdx) {
            if (!partialLoad_.load())
                presetManager_->saveSlot(autoIdx);
            return;
        }

        // Save current A-E slot first
        if (presetManager_) {
            int currentSlot = presetManager_->getActiveSlot();
            if (currentSlot >= 0 && !partialLoad_.load())
                presetManager_->saveSlot(currentSlot);
        }
        partialLoad_ = false;

        // If Auto preset file doesn't exist yet, create default (3 built-in processors)
        auto autoFile = PresetManager::getSlotFile(autoIdx);
        if (!autoFile.existsAsFile()) {
            loadingSlot_ = true;
            // Clear chain and add auto processors
            auto& chain = audioEngine_.getVSTChain();
            while (chain.getPluginCount() > 0) {
                if (!chain.removePlugin(chain.getPluginCount() - 1)) {
                    showNotification("Auto setup: failed to remove plugin", NotificationLevel::Error);
                    loadingSlot_ = false;
                    return;
                }
            }
            auto result = chain.addAutoProcessors();
            if (!result.success) {
                showNotification("Auto setup failed: " + result.message, NotificationLevel::Error);
                loadingSlot_ = false;
                return;
            }
            presetManager_->setActiveSlot(autoIdx);
            presetManager_->saveSlot(autoIdx);
            loadingSlot_ = false;

            // Deselect A-E, highlight Auto button
            if (presetSlotBar_)
                presetSlotBar_->setActiveSlot(autoIdx);
            updateAutoButtonVisual();
            pluginChainEditor_->refreshList();
            markSettingsDirty();
        } else {
            // Load existing Auto preset (same flow as A-E slot switch)
            loadingSlot_ = true;
            if (presetSlotBar_) {
                presetSlotBar_->setSlotButtonsEnabled(false);
                presetSlotBar_->setActiveSlot(autoIdx);
            }
            pluginChainEditor_->showLoadingState();
            updateAutoButtonVisual();

            auto safeThis = juce::Component::SafePointer<MainComponent>(this);
            presetManager_->loadSlotAsync(autoIdx,
                [safeThis](bool ok) {
                    if (!safeThis) return;
                    safeThis->loadingSlot_ = false;
                    safeThis->partialLoad_ = !ok;
                    if (safeThis->presetSlotBar_)
                        safeThis->presetSlotBar_->setSlotButtonsEnabled(true);
                    if (safeThis->pluginChainEditor_)
                        safeThis->pluginChainEditor_->hideLoadingState();
                    safeThis->refreshUI();
                    if (safeThis->presetSlotBar_)
                        safeThis->presetSlotBar_->updateSlotButtonStates();
                    safeThis->updateAutoButtonVisual();
                    if (ok) safeThis->markSettingsDirty();
                });
        }
    };
    // Right-click context menu for Auto button (Reset to defaults)
    autoProcessorBtn_.setTriggeredOnMouseDown(false);
    autoProcessorBtn_.addMouseListener(this, false);
    addAndMakeVisible(autoProcessorBtn_);

    // ── Output Panel ──
    outputPanel_ = std::make_unique<OutputPanel>(audioEngine_);
    outputPanel_->onSettingsChanged = [this] { markSettingsDirty(); };
    outputPanel_->onError = [this](const juce::String& msg) {
        showNotification(msg, NotificationLevel::Error);
    };
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
            if (presetSlotBar_)
                presetSlotBar_->updateSlotButtonStates();
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
            if (settingsAutosaver_)
                settingsAutosaver_->loadFromFile();
            loadingSlot_ = false;
            refreshUI();
            if (presetSlotBar_)
                presetSlotBar_->updateSlotButtonStates();
            pluginChainEditor_->refreshList();
        };
        settingsPanel->onSaveSettings = [safeThis = juce::Component::SafePointer<MainComponent>(this)] {
            SettingsExporter::showSaveDialog("DirectPipe_backup.dpbackup", "*.dpbackup", "dpbackup",
                [safeThis]() -> juce::String {
                    if (!safeThis) return {};
                    return SettingsExporter::exportAll(*safeThis->presetManager_,
                                                       safeThis->controlManager_->getConfigStore());
                });
        };
        settingsPanel->onLoadSettings = [safeThis = juce::Component::SafePointer<MainComponent>(this)] {
            SettingsExporter::showLoadDialog("*.dpbackup",
                [safeThis](const juce::String& json) -> bool {
                    if (!safeThis) return false;
                    safeThis->loadingSlot_ = true;
                    if (!SettingsExporter::importAll(json, *safeThis->presetManager_,
                                                     safeThis->controlManager_->getConfigStore())) {
                        safeThis->loadingSlot_ = false;
                        return false;
                    }
                    safeThis->controlManager_->reloadConfig();
                    safeThis->loadingSlot_ = false;
                    safeThis->markSettingsDirty();
                    safeThis->refreshUI();
                    safeThis->presetSlotBar_->updateSlotButtonStates();
                    return true;
                });
        };
        settingsPanel->onFullBackup = [safeThis = juce::Component::SafePointer<MainComponent>(this)] {
            SettingsExporter::showSaveDialog("DirectPipe_full.dpfullbackup", "*.dpfullbackup", "dpfullbackup",
                [safeThis]() -> juce::String {
                    if (!safeThis) return {};
                    auto json = SettingsExporter::exportFullBackup(*safeThis->presetManager_,
                                                                    safeThis->controlManager_->getConfigStore());
                    juce::Logger::writeToLog("[APP] Full backup saved");
                    return json;
                });
        };
        settingsPanel->onFullRestore = [safeThis = juce::Component::SafePointer<MainComponent>(this)] {
            SettingsExporter::showLoadDialog("*.dpfullbackup",
                [safeThis](const juce::String& json) -> bool {
                    if (!safeThis) return false;
                    safeThis->loadingSlot_ = true;
                    if (!SettingsExporter::importFullBackup(json, *safeThis->presetManager_,
                                                            safeThis->controlManager_->getConfigStore())) {
                        safeThis->loadingSlot_ = false;
                        return false;
                    }
                    safeThis->controlManager_->reloadConfig();
                    safeThis->presetManager_->refreshSlotOccupancy();
                    safeThis->presetManager_->loadSlotNames();
                    safeThis->presetManager_->invalidatePreloadCache();
                    safeThis->loadingSlot_ = false;
                    safeThis->markSettingsDirty();
                    safeThis->refreshUI();
                    safeThis->presetSlotBar_->updateSlotButtonStates();
                    safeThis->pluginChainEditor_->refreshList();
                    juce::Logger::writeToLog("[APP] Full backup restored");
                    return true;
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
    // Safety Limiter toggle from chain editor UI
    pluginChainEditor_->onLimiterToggled = [this](bool enabled) {
        audioEngine_.getSafetyLimiter().setEnabled(enabled);
        markSettingsDirty();
    };
    pluginChainEditor_->onLimiterCeilingChanged = [this](float dB) {
        audioEngine_.getSafetyLimiter().setCeiling(dB);
        markSettingsDirty();
    };
    pluginChainEditor_->setLimiterState(audioEngine_.getSafetyLimiter().isEnabled());
    pluginChainEditor_->setLimiterCeiling(audioEngine_.getSafetyLimiter().getCeilingdB());

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
                safeThis->presetSlotBar_->updateSlotButtonStates();
            }
        });
    };
    addAndMakeVisible(loadPresetBtn_);

    // ── Quick Preset Slot Buttons (A..E) ──
    presetSlotBar_ = std::make_unique<PresetSlotBar>(
        *presetManager_, audioEngine_, *pluginChainEditor_, loadingSlot_, partialLoad_);
    presetSlotBar_->onSettingsDirty = [this] { markSettingsDirty(); };
    presetSlotBar_->onRefreshUI = [this] { refreshUI(); };
    presetSlotBar_->onNotification = [this](const juce::String& msg, NotificationLevel level) {
        showNotification(msg, level);
    };
    addAndMakeVisible(*presetSlotBar_);

    // ── Action Handler (routes ActionEvents to engine/UI) ──
    actionHandler_ = std::make_unique<ActionHandler>(
        audioEngine_, *presetManager_, *presetSlotBar_, loadingSlot_, partialLoad_);
    actionHandler_->onDirty = [this] { markSettingsDirty(); };
    actionHandler_->onInputGainSync = [this](float gain) {
        inputGainSlider_.setValue(gain, juce::dontSendNotification);
    };
    actionHandler_->onPanicStateChanged = [this](bool muted) {
        panicMuteBtn_.setButtonText(muted ? "UNMUTE" : "PANIC MUTE");
        panicMuteBtn_.setColour(juce::TextButton::buttonColourId,
                                juce::Colour(muted ? 0xFF4CAF50u : 0xFFE05050u));
    };
    actionHandler_->onIpcStateChanged = [this](bool enabled) {
        if (auto* outPanel = dynamic_cast<OutputPanel*>(rightTabs_->getTabContentComponent(1)))
            outPanel->setIpcToggleState(enabled);
    };
    actionHandler_->onNotification = [this](const juce::String& msg, NotificationLevel level) {
        showNotification(msg, level);
    };
    actionHandler_->getRecordingFolder = [this]() -> juce::File {
        return outputPanelPtr_ ? outputPanelPtr_->getRecordingFolder()
            : juce::File::getSpecialLocation(
                juce::File::userDocumentsDirectory).getChildFile("DirectPipe Recordings");
    };
    actionHandler_->onRecordingStopped = [this](const juce::File& file) {
        if (outputPanelPtr_)
            outputPanelPtr_->setLastRecordedFile(file);
    };
    actionHandler_->onAutoPresetSwitch = [this] {
        // Trigger the Auto button's onClick handler (same behavior for HTTP/WS/MIDI)
        if (autoProcessorBtn_.onClick)
            autoProcessorBtn_.onClick();
    };

    // ── Settings Autosaver (dirty-flag + debounce + save/load) ──
    settingsAutosaver_ = std::make_unique<SettingsAutosaver>(
        *presetManager_, audioEngine_, loadingSlot_, partialLoad_);
    settingsAutosaver_->onRestorePanicMute = [this] {
        actionHandler_->restorePanicMuteFromSettings();
    };
    settingsAutosaver_->onPostLoad = [this] {
        refreshUI();
        presetSlotBar_->updateSlotButtonStates();
    };
    settingsAutosaver_->onFlushLogs = [this] {
        if (auto* sp = dynamic_cast<LogPanel*>(rightTabs_->getTabContentComponent(3)))
            sp->flushPendingLogs();
    };
    settingsAutosaver_->onShowWindow = [safeThis = juce::Component::SafePointer<MainComponent>(this)] {
        if (!safeThis) return;
        if (auto* w = safeThis->getTopLevelComponent())
            w->setVisible(true);
    };

    // ── Status Updater (mute indicators, status bar, broadcaster) ──
    statusUpdater_ = std::make_unique<StatusUpdater>(audioEngine_, broadcaster_);

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
    outputMuteBtn_.onClick = [this] { actionHandler_->toggleOutputMute(); };
    monitorMuteBtn_.onClick = [this] { actionHandler_->toggleMonitorMute(); };
    vstMuteBtn_.onClick = [this] { actionHandler_->toggleIpcMute(); };

    // ── Panic Mute Button ──
    panicMuteBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
    panicMuteBtn_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    panicMuteBtn_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    panicMuteBtn_.onClick = [this] { actionHandler_->togglePanicMute(); };
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

    // Bind UI component pointers to StatusUpdater (all components created above)
    statusUpdater_->setUI(&latencyLabel_, &cpuLabel_, &formatLabel_,
                          &outputMuteBtn_, &monitorMuteBtn_, &vstMuteBtn_,
                          &inputGainSlider_, inputMeter_.get(), outputMeter_.get());

    // Start UI update timer (30 Hz)
    startTimerHz(30);

    setSize(kDefaultWidth, kDefaultHeight);

    // Auto-load last saved settings
    settingsAutosaver_->loadFromFile();

    // First launch: auto-select slot A
    if (presetManager_->getActiveSlot() < 0) {
        presetManager_->setActiveSlot(0);
    }
    presetSlotBar_->updateSlotButtonStates();
    updateAutoButtonVisual();

    // Update checker: set callbacks and start
    updateChecker_.onUpdateAvailable = [safeThis = juce::Component::SafePointer<MainComponent>(this)](
            const juce::String& version, const juce::String& /*downloadUrl*/) {
        if (!safeThis) return;
        safeThis->creditLink_.setButtonText(
            "NEW v" + version + " | v" +
            juce::String(ProjectInfo::versionString) + " | Created by LiveTrack");
        safeThis->creditLink_.setColour(juce::HyperlinkButton::textColourId,
                                        juce::Colour(0xFFFFAA33));
        safeThis->creditLink_.setURL({});
        safeThis->creditLink_.onClick = [safeThis]() {
            if (safeThis) safeThis->updateChecker_.showUpdateDialog();
        };
    };
    updateChecker_.onPostUpdateNotification = [safeThis = juce::Component::SafePointer<MainComponent>(this)](
            const juce::String& version) {
        if (!safeThis) return;
        safeThis->showNotification("Updated to v" + version + " successfully!",
                                   NotificationLevel::Info);
    };
    updateChecker_.cleanupPreviousUpdate();
    updateChecker_.checkForUpdate();

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
    settingsAutosaver_->saveNow();
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
    actionHandler_->handle(event);
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

    // Input gain row: [Gain:] [slider] [Auto]
    {
        int autoBtnW = 56;
        int autoBtnH = 28;
        int gap = 6;
        inputGainLabel_.setBounds(cx, y, 40, 24);
        inputGainSlider_.setBounds(cx + 44, y, cw - 44 - autoBtnW - gap, 24);
        autoProcessorBtn_.setBounds(cx + cw - autoBtnW, y - 2, autoBtnW, autoBtnH);
    }
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

    // Quick preset slot buttons (A..E) — layout handled by PresetSlotBar::resized()
    presetSlotBar_->setBounds(cx, y, cw, 26);
    y += 30;

    // Bottom controls: OUT/MON/VST indicators (30px) + gap (4px) + PANIC MUTE (32px) = 66px
    int bottomControlsH = 30 + 4 + 32;
    int bottomY = bounds.getBottom() - kStatusBarHeight - bottomControlsH;

    // Plugin chain fills remaining space between preset slots and bottom controls
    int chainH = bottomY - y - 4;
    pluginChainEditor_->setBounds(cx, y, cw, chainH);

    // Mute status indicators (OUT / MON / VST)
    {
        int gap = 4;
        int indicatorW = (cw - gap * 2) / 3;
        int lastW = cw - (indicatorW + gap) * 2;
        outputMuteBtn_.setBounds(cx, bottomY, indicatorW, 30);
        monitorMuteBtn_.setBounds(cx + indicatorW + gap, bottomY, indicatorW, 30);
        vstMuteBtn_.setBounds(cx + (indicatorW + gap) * 2, bottomY, lastW, 30);
    }

    // PANIC MUTE button (below indicators: 30px height + 4px gap)
    panicMuteBtn_.setBounds(cx, bottomY + 30 + 4, cw, 32);

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

// ─── Auto button right-click menu ───────────────────────────────────────────

void MainComponent::mouseDown(const juce::MouseEvent& e)
{
    // Right-click on Auto button shows context menu
    if (e.mods.isPopupMenu() && e.eventComponent == &autoProcessorBtn_) {
        juce::PopupMenu menu;
        menu.addItem(1, "Reset Auto to Defaults");

        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&autoProcessorBtn_),
            [this](int menuResult) {
                if (menuResult == 1) {
                    // Reset: clear chain, add default processors, save
                    auto& chain = audioEngine_.getVSTChain();

                    // Remove all plugins from chain
                    while (chain.getPluginCount() > 0) {
                        if (!chain.removePlugin(0)) {
                            showNotification("Auto reset: failed to remove plugin", NotificationLevel::Error);
                            return;
                        }
                    }

                    // Add default built-in processors
                    auto result = chain.addAutoProcessors();
                    if (!result.success) {
                        showNotification("Auto reset failed: " + result.message, NotificationLevel::Error);
                        return;
                    }

                    // Save to Auto slot
                    int autoIdx = PresetSlotBar::kAutoSlotIndex;
                    if (presetManager_) {
                        presetManager_->setActiveSlot(autoIdx);
                        presetManager_->saveSlot(autoIdx);
                    }

                    if (pluginChainEditor_)
                        pluginChainEditor_->refreshList();
                    updateAutoButtonVisual();
                    markSettingsDirty();
                }
            });
        return;
    }

    juce::Component::mouseDown(e);
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

    // ── Status bar, mute indicators, level meters, broadcaster ──
    statusUpdater_->tick(presetManager_.get(), PresetManager::kNumSlots);

    // Sync Auto button visual (active/inactive)
    updateAutoButtonVisual();

    // Sync limiter toggle + ceiling + GR in chain editor
    if (pluginChainEditor_) {
        pluginChainEditor_->setLimiterState(audioEngine_.getSafetyLimiter().isEnabled());
        pluginChainEditor_->setLimiterCeiling(audioEngine_.getSafetyLimiter().getCeilingdB());
        pluginChainEditor_->setLimiterGR(audioEngine_.getSafetyLimiter().getCurrentGainReduction());
    }

    // Update recording state in OutputPanel (Monitor tab)
    if (outputPanelPtr_) {
        bool isRec = audioEngine_.getRecorder().isRecording();
        double recSecs = audioEngine_.getRecorder().getRecordedSeconds();
        outputPanelPtr_->updateRecordingState(isRec, recSecs);
    }

    // Device reconnection check (~3s interval fallback for missed change notifications)
    audioEngine_.checkReconnection();

    // Dirty-flag auto-save with 1-second debounce
    settingsAutosaver_->tick();
}

// ─── Settings dirty flag ─────────────────────────────────────────────────────

void MainComponent::markSettingsDirty()
{
    settingsAutosaver_->markDirty();
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

    // Auto button visual state
    updateAutoButtonVisual();
}

// ─── Auto Button Visual ───────────────────────────────────────────────────

void MainComponent::updateAutoButtonVisual()
{
    bool autoActive = (presetManager_ && presetManager_->getActiveSlot() == PresetSlotBar::kAutoSlotIndex);
    autoProcessorBtn_.setColour(juce::TextButton::buttonColourId,
        juce::Colour(autoActive ? 0xFF4CAF50 : 0xFF2A4035));
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

} // namespace directpipe
