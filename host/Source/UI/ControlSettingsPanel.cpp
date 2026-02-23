/**
 * @file ControlSettingsPanel.cpp
 * @brief Tabbed control settings panel implementation
 */

#include "ControlSettingsPanel.h"

namespace directpipe {

static juce::String actionToDisplayName(const ActionEvent& event)
{
    if (!event.stringParam.empty())
        return juce::String(event.stringParam);

    switch (event.action) {
        case Action::PluginBypass:    return "Plugin " + juce::String(event.intParam + 1) + " Bypass";
        case Action::MasterBypass:    return "Master Bypass";
        case Action::SetVolume:       return "Set Volume";
        case Action::ToggleMute:      return "Toggle Mute";
        case Action::LoadPreset:      return "Load Preset " + juce::String(event.intParam);
        case Action::PanicMute:       return "Panic Mute";
        case Action::InputGainAdjust: return "Input Gain Adjust";
        case Action::NextPreset:      return "Next Preset";
        case Action::PreviousPreset:  return "Previous Preset";
        case Action::InputMuteToggle: return "Input Mute Toggle";
        default:                      return "Unknown";
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  HotkeyTab implementation
// ═════════════════════════════════════════════════════════════════════════════

HotkeyTab::HotkeyTab(ControlManager& manager)
    : manager_(manager)
{
    headerLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    headerLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(headerLabel_);

    addButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kAccentColour));
    addButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    addButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    addButton_.onClick = [this] { onAddClicked(); };
    addAndMakeVisible(addButton_);

    statusLabel_.setFont(juce::Font(12.0f));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(kWarningColour));
    addAndMakeVisible(statusLabel_);

    viewport_.setViewedComponent(&rowContainer_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    refreshBindings();

    // Poll for recording completion at 10 Hz
    startTimerHz(10);
}

HotkeyTab::~HotkeyTab()
{
    stopTimer();

    // Cancel any in-progress recording
    if (recordingIndex_ >= 0) {
        manager_.getHotkeyHandler().stopRecording();
    }
}

void HotkeyTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));
}

void HotkeyTab::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH = 28;
    constexpr int gap  = 6;

    int y = bounds.getY();

    // Header + Add button
    headerLabel_.setBounds(bounds.getX(), y, bounds.getWidth() - 130, rowH);
    addButton_.setBounds(bounds.getRight() - 120, y, 120, rowH);
    y += rowH + gap;

    // Status line
    statusLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), 20);
    y += 20 + gap;

    // Viewport fills the rest
    viewport_.setBounds(bounds.getX(), y, bounds.getWidth(), bounds.getBottom() - y);

    // Lay out rows inside the row container
    constexpr int innerRowH = 30;
    constexpr int innerGap  = 2;
    int totalH = static_cast<int>(rows_.size()) * (innerRowH + innerGap);
    rowContainer_.setSize(viewport_.getWidth() - viewport_.getScrollBarThickness(), totalH);

    int ry = 0;
    int containerW = rowContainer_.getWidth();
    constexpr int actionW = 160;
    constexpr int btnW    = 50;
    constexpr int removeBtnW = 28;

    for (auto* row : rows_) {
        int shortcutW = containerW - actionW - btnW - removeBtnW - gap * 3;

        row->actionLabel.setBounds(0, ry, actionW, innerRowH);
        row->shortcutLabel.setBounds(actionW + gap, ry, shortcutW, innerRowH);
        row->setButton.setBounds(actionW + gap + shortcutW + gap, ry, btnW, innerRowH);
        row->removeButton.setBounds(containerW - removeBtnW, ry, removeBtnW, innerRowH);

        ry += innerRowH + innerGap;
    }
}

void HotkeyTab::refreshBindings()
{
    // Remove old rows from the container
    for (auto* row : rows_) {
        rowContainer_.removeChildComponent(&row->actionLabel);
        rowContainer_.removeChildComponent(&row->shortcutLabel);
        rowContainer_.removeChildComponent(&row->setButton);
        rowContainer_.removeChildComponent(&row->removeButton);
    }
    rows_.clear();

    auto& handler = manager_.getHotkeyHandler();
    const auto& bindings = handler.getBindings();

    for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
        auto* row = new BindingRow();
        const auto& binding = bindings[static_cast<size_t>(i)];

        // Action label
        row->actionLabel.setText(actionToDisplayName(binding.action),
                                 juce::dontSendNotification);
        row->actionLabel.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
        row->actionLabel.setFont(juce::Font(12.0f));

        // Shortcut label
        row->shortcutLabel.setText(juce::String(binding.displayName),
                                   juce::dontSendNotification);
        row->shortcutLabel.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
        row->shortcutLabel.setFont(juce::Font(12.0f, juce::Font::bold));
        row->shortcutLabel.setJustificationType(juce::Justification::centredLeft);

        // Alternating row background via opaque colour
        if (i % 2 == 1) {
            row->actionLabel.setColour(juce::Label::backgroundColourId,
                                       juce::Colour(kRowAltColour));
            row->shortcutLabel.setColour(juce::Label::backgroundColourId,
                                         juce::Colour(kRowAltColour));
        }

        // [Set] button
        row->setButton.setColour(juce::TextButton::buttonColourId,
                                 juce::Colour(kSurfaceColour));
        row->setButton.setColour(juce::TextButton::textColourOnId,
                                 juce::Colour(kTextColour));
        row->setButton.setColour(juce::TextButton::textColourOffId,
                                 juce::Colour(kTextColour));
        row->setButton.onClick = [this, i] { onSetClicked(i); };

        // [X] remove button
        row->removeButton.setColour(juce::TextButton::buttonColourId,
                                    juce::Colour(kSurfaceColour));
        row->removeButton.setColour(juce::TextButton::textColourOnId,
                                    juce::Colour(0xFFE05050));
        row->removeButton.setColour(juce::TextButton::textColourOffId,
                                    juce::Colour(0xFFE05050));
        row->removeButton.onClick = [this, i] { onRemoveClicked(i); };

        rowContainer_.addAndMakeVisible(row->actionLabel);
        rowContainer_.addAndMakeVisible(row->shortcutLabel);
        rowContainer_.addAndMakeVisible(row->setButton);
        rowContainer_.addAndMakeVisible(row->removeButton);

        rows_.add(row);
    }

    resized();
}

void HotkeyTab::timerCallback()
{
    // Check if recording mode has ended (handler will clear isRecording)
    // recordingIndex_ >= 0 = editing existing, -2 = adding new
    if (recordingIndex_ != -1 && !manager_.getHotkeyHandler().isRecording()) {
        recordingIndex_ = -1;
        statusLabel_.setText("", juce::dontSendNotification);
        refreshBindings();
    }
}

void HotkeyTab::onSetClicked(int bindingIndex)
{
    if (recordingIndex_ >= 0) {
        // Already recording — cancel first
        manager_.getHotkeyHandler().stopRecording();
    }

    recordingIndex_ = bindingIndex;
    statusLabel_.setText("Press a key combination...", juce::dontSendNotification);

    // Highlight the active row
    if (bindingIndex >= 0 && bindingIndex < rows_.size()) {
        rows_[bindingIndex]->shortcutLabel.setText("...", juce::dontSendNotification);
        rows_[bindingIndex]->shortcutLabel.setColour(juce::Label::textColourId,
                                                      juce::Colour(kWarningColour));
    }

    auto& handler = manager_.getHotkeyHandler();
    const auto& bindings = handler.getBindings();

    if (bindingIndex < 0 || bindingIndex >= static_cast<int>(bindings.size()))
        return;

    // Capture the action from the binding we want to re-map
    ActionEvent targetAction = bindings[static_cast<size_t>(bindingIndex)].action;

    handler.startRecording(
        [this, bindingIndex, targetAction](uint32_t mods, uint32_t vk, const std::string& name) {
            // Remove old binding and register new one
            auto& h = manager_.getHotkeyHandler();
            const auto& currentBindings = h.getBindings();
            if (bindingIndex < static_cast<int>(currentBindings.size())) {
                h.unregisterHotkey(currentBindings[static_cast<size_t>(bindingIndex)].id);
            }
            h.registerHotkey(mods, vk, targetAction, name);
            manager_.saveConfig();

            // UI refresh will happen in timerCallback when isRecording() returns false
        });
}

void HotkeyTab::onRemoveClicked(int bindingIndex)
{
    auto& handler = manager_.getHotkeyHandler();
    const auto& bindings = handler.getBindings();

    if (bindingIndex >= 0 && bindingIndex < static_cast<int>(bindings.size())) {
        handler.unregisterHotkey(bindings[static_cast<size_t>(bindingIndex)].id);
        manager_.saveConfig();
        // Defer refresh to avoid deleting the button component from within its own callback
        juce::MessageManager::callAsync([this] { refreshBindings(); });
    }
}

juce::PopupMenu HotkeyTab::buildActionMenu()
{
    juce::PopupMenu menu;

    // Plugin bypass (1-8)
    juce::PopupMenu bypassMenu;
    for (int i = 1; i <= 8; ++i) {
        ActionEvent evt{Action::PluginBypass, i - 1, 0.0f,
                        "Plugin " + std::to_string(i) + " Bypass"};
        bypassMenu.addItem(100 + i, "Plugin " + juce::String(i) + " Bypass");
    }
    menu.addSubMenu("Plugin Bypass", bypassMenu);

    // Master bypass
    menu.addItem(200, "Master Bypass");

    // Mute / Panic
    menu.addItem(201, "Panic Mute");
    menu.addItem(202, "Input Mute Toggle");

    // Input gain
    menu.addItem(300, "Input Gain +1 dB");
    menu.addItem(301, "Input Gain -1 dB");

    // Presets
    juce::PopupMenu presetMenu;
    for (int i = 1; i <= 8; ++i) {
        presetMenu.addItem(400 + i, "Load Preset " + juce::String(i));
    }
    menu.addSubMenu("Load Preset", presetMenu);

    menu.addItem(500, "Next Preset");
    menu.addItem(501, "Previous Preset");

    return menu;
}

void HotkeyTab::onAddClicked()
{
    auto menu = buildActionMenu();

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&addButton_),
        [this](int result) {
            if (result == 0) return;  // cancelled

            ActionEvent action;

            if (result >= 100 && result < 200) {
                int pluginIdx = result - 100 - 1;
                action = {Action::PluginBypass, pluginIdx, 0.0f,
                          "Plugin " + std::to_string(pluginIdx + 1) + " Bypass"};
            } else if (result == 200) {
                action = {Action::MasterBypass, 0, 0.0f, "Master Bypass"};
            } else if (result == 201) {
                action = {Action::PanicMute, 0, 0.0f, "Panic Mute"};
            } else if (result == 202) {
                action = {Action::InputMuteToggle, 0, 0.0f, "Input Mute Toggle"};
            } else if (result == 300) {
                action = {Action::InputGainAdjust, 0, 1.0f, "Input Gain +1 dB"};
            } else if (result == 301) {
                action = {Action::InputGainAdjust, 0, -1.0f, "Input Gain -1 dB"};
            } else if (result >= 400 && result < 500) {
                int presetIdx = result - 400;
                action = {Action::LoadPreset, presetIdx, 0.0f,
                          "Load Preset " + std::to_string(presetIdx)};
            } else if (result == 500) {
                action = {Action::NextPreset, 0, 0.0f, "Next Preset"};
            } else if (result == 501) {
                action = {Action::PreviousPreset, 0, 0.0f, "Previous Preset"};
            } else {
                return;
            }

            // Enter recording mode to capture key combination
            statusLabel_.setText("Press a key combination for: " +
                                 juce::String(action.stringParam),
                                 juce::dontSendNotification);
            recordingIndex_ = -2;  // special value for "new binding"

            manager_.getHotkeyHandler().startRecording(
                [this, action](uint32_t mods, uint32_t vk, const std::string& name) {
                    manager_.getHotkeyHandler().registerHotkey(mods, vk, action, name);
                    manager_.saveConfig();
                    // UI refresh happens in timerCallback
                });
        });
}

// ═════════════════════════════════════════════════════════════════════════════
//  MidiTab implementation
// ═════════════════════════════════════════════════════════════════════════════

MidiTab::MidiTab(ControlManager& manager)
    : manager_(manager)
{
    // Device selector
    deviceLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(deviceLabel_);

    deviceCombo_.onChange = [this] { onDeviceSelected(); };
    addAndMakeVisible(deviceCombo_);

    rescanButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    rescanButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    rescanButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    rescanButton_.onClick = [this] { onRescanClicked(); };
    addAndMakeVisible(rescanButton_);

    // Mapping header
    mappingHeaderLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    mappingHeaderLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(mappingHeaderLabel_);

    // Status label
    statusLabel_.setFont(juce::Font(12.0f));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(kWarningColour));
    addAndMakeVisible(statusLabel_);

    // Scrollable viewport
    viewport_.setViewedComponent(&rowContainer_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    refreshAll();

    // Poll for learn completion at 10 Hz
    startTimerHz(10);
}

MidiTab::~MidiTab()
{
    stopTimer();

    if (learningIndex_ >= 0) {
        manager_.getMidiHandler().stopLearn();
    }
}

void MidiTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));
}

void MidiTab::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH = 28;
    constexpr int gap  = 6;
    constexpr int labelW = 100;
    constexpr int btnW   = 70;

    int y = bounds.getY();

    // Device selector row
    deviceLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    rescanButton_.setBounds(bounds.getRight() - btnW, y, btnW, rowH);
    deviceCombo_.setBounds(bounds.getX() + labelW + gap, y,
                           bounds.getWidth() - labelW - btnW - gap * 2, rowH);
    y += rowH + gap;

    // Mapping header
    mappingHeaderLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    // Status label
    statusLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), 20);
    y += 20 + gap;

    // Viewport fills the rest
    viewport_.setBounds(bounds.getX(), y, bounds.getWidth(), bounds.getBottom() - y);

    // Layout rows inside row container
    constexpr int innerRowH = 30;
    constexpr int innerGap  = 2;
    int totalH = static_cast<int>(rows_.size()) * (innerRowH + innerGap);
    rowContainer_.setSize(viewport_.getWidth() - viewport_.getScrollBarThickness(), totalH);

    int ry = 0;
    int containerW = rowContainer_.getWidth();
    constexpr int controlW   = 120;
    constexpr int learnBtnW  = 55;
    constexpr int removeBtnW = 28;

    for (auto* row : rows_) {
        int actionW = containerW - controlW - learnBtnW - removeBtnW - gap * 3;

        row->controlLabel.setBounds(0, ry, controlW, innerRowH);
        row->actionLabel.setBounds(controlW + gap, ry, actionW, innerRowH);
        row->learnButton.setBounds(controlW + gap + actionW + gap, ry, learnBtnW, innerRowH);
        row->removeButton.setBounds(containerW - removeBtnW, ry, removeBtnW, innerRowH);

        ry += innerRowH + innerGap;
    }
}

void MidiTab::refreshAll()
{
    refreshDeviceList();
    refreshMappings();
}

void MidiTab::refreshDeviceList()
{
    deviceCombo_.clear(juce::dontSendNotification);

    auto devices = manager_.getMidiHandler().getAvailableDevices();
    for (int i = 0; i < devices.size(); ++i) {
        deviceCombo_.addItem(devices[i], i + 1);
    }

    if (devices.size() > 0) {
        deviceCombo_.setSelectedId(1, juce::dontSendNotification);
    }
}

void MidiTab::refreshMappings()
{
    // Remove old rows from the container
    for (auto* row : rows_) {
        rowContainer_.removeChildComponent(&row->controlLabel);
        rowContainer_.removeChildComponent(&row->actionLabel);
        rowContainer_.removeChildComponent(&row->learnButton);
        rowContainer_.removeChildComponent(&row->removeButton);
    }
    rows_.clear();

    auto& handler = manager_.getMidiHandler();
    const auto& bindings = handler.getBindings();

    for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
        auto* row = new MappingRow();
        const auto& binding = bindings[static_cast<size_t>(i)];

        // Control label (CC/Note info)
        row->controlLabel.setText(midiBindingToString(binding), juce::dontSendNotification);
        row->controlLabel.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
        row->controlLabel.setFont(juce::Font(12.0f, juce::Font::bold));

        // Action label
        row->actionLabel.setText(actionToDisplayName(binding.action),
                                 juce::dontSendNotification);
        row->actionLabel.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
        row->actionLabel.setFont(juce::Font(12.0f));

        // Alternating row background
        if (i % 2 == 1) {
            row->controlLabel.setColour(juce::Label::backgroundColourId,
                                        juce::Colour(kRowAltColour));
            row->actionLabel.setColour(juce::Label::backgroundColourId,
                                       juce::Colour(kRowAltColour));
        }

        // [Learn] button
        row->learnButton.setColour(juce::TextButton::buttonColourId,
                                   juce::Colour(kSurfaceColour));
        row->learnButton.setColour(juce::TextButton::textColourOnId,
                                   juce::Colour(kTextColour));
        row->learnButton.setColour(juce::TextButton::textColourOffId,
                                   juce::Colour(kTextColour));
        row->learnButton.onClick = [this, i] { onLearnClicked(i); };

        // [X] remove button
        row->removeButton.setColour(juce::TextButton::buttonColourId,
                                    juce::Colour(kSurfaceColour));
        row->removeButton.setColour(juce::TextButton::textColourOnId,
                                    juce::Colour(0xFFE05050));
        row->removeButton.setColour(juce::TextButton::textColourOffId,
                                    juce::Colour(0xFFE05050));
        row->removeButton.onClick = [this, i] { onRemoveClicked(i); };

        rowContainer_.addAndMakeVisible(row->controlLabel);
        rowContainer_.addAndMakeVisible(row->actionLabel);
        rowContainer_.addAndMakeVisible(row->learnButton);
        rowContainer_.addAndMakeVisible(row->removeButton);

        rows_.add(row);
    }

    resized();
}

void MidiTab::timerCallback()
{
    // Check if learn mode has ended
    if (learningIndex_ >= 0 && !manager_.getMidiHandler().isLearning()) {
        learningIndex_ = -1;
        statusLabel_.setText("", juce::dontSendNotification);
        refreshMappings();
    }
}

void MidiTab::onDeviceSelected()
{
    auto selectedText = deviceCombo_.getText();
    if (selectedText.isNotEmpty()) {
        manager_.getMidiHandler().openDevice(selectedText);
    }
}

void MidiTab::onRescanClicked()
{
    manager_.getMidiHandler().rescanDevices();
    refreshDeviceList();
}

void MidiTab::onLearnClicked(int mappingIndex)
{
    if (learningIndex_ >= 0) {
        manager_.getMidiHandler().stopLearn();
    }

    learningIndex_ = mappingIndex;
    statusLabel_.setText("Move a MIDI control...", juce::dontSendNotification);

    // Highlight the active row
    if (mappingIndex >= 0 && mappingIndex < rows_.size()) {
        rows_[mappingIndex]->controlLabel.setText("...", juce::dontSendNotification);
        rows_[mappingIndex]->controlLabel.setColour(juce::Label::textColourId,
                                                     juce::Colour(kWarningColour));
    }

    auto& handler = manager_.getMidiHandler();
    const auto& bindings = handler.getBindings();

    if (mappingIndex < 0 || mappingIndex >= static_cast<int>(bindings.size()))
        return;

    // Capture the action from the existing mapping
    ActionEvent targetAction = bindings[static_cast<size_t>(mappingIndex)].action;

    handler.startLearn(
        [this, mappingIndex, targetAction](int cc, int note, int channel,
                                            const juce::String& deviceName) {
            auto& h = manager_.getMidiHandler();

            // Remove old binding
            if (mappingIndex < static_cast<int>(h.getBindings().size())) {
                h.removeBinding(mappingIndex);
            }

            // Create new binding with learned CC/Note
            MidiBinding newBinding;
            newBinding.cc = cc;
            newBinding.note = note;
            newBinding.channel = channel;
            newBinding.deviceName = deviceName.toStdString();
            newBinding.action = targetAction;
            newBinding.type = (cc >= 0) ? MidiMappingType::Toggle : MidiMappingType::NoteOnOff;
            h.addBinding(newBinding);

            manager_.saveConfig();
            // UI refresh will happen in timerCallback when isLearning() returns false
        });
}

void MidiTab::onRemoveClicked(int mappingIndex)
{
    auto& handler = manager_.getMidiHandler();

    if (mappingIndex >= 0 && mappingIndex < static_cast<int>(handler.getBindings().size())) {
        handler.removeBinding(mappingIndex);
        manager_.saveConfig();
        // Defer refresh to avoid deleting the button component from within its own callback
        juce::MessageManager::callAsync([this] { refreshMappings(); });
    }
}

juce::String MidiTab::midiBindingToString(const MidiBinding& binding)
{
    juce::String result;

    if (binding.cc >= 0) {
        result = "CC " + juce::String(binding.cc);
    } else if (binding.note >= 0) {
        result = "Note " + juce::String(binding.note);
    } else {
        result = "(unset)";
    }

    if (binding.channel > 0) {
        result += " Ch " + juce::String(binding.channel);
    } else {
        result += " Ch *";
    }

    return result;
}

// ═════════════════════════════════════════════════════════════════════════════
//  StreamDeckTab implementation
// ═════════════════════════════════════════════════════════════════════════════

StreamDeckTab::StreamDeckTab(ControlManager& manager)
    : manager_(manager)
{
    // ── WebSocket section ──
    wsSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    wsSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsSectionLabel_);

    wsPortLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsPortLabel_);
    wsPortValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    wsPortValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(wsPortValueLabel_);

    wsStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsStatusLabel_);
    wsStatusValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(wsStatusValueLabel_);

    wsClientsLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(wsClientsLabel_);
    wsClientsValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    wsClientsValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(wsClientsValueLabel_);

    wsToggleButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    wsToggleButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    wsToggleButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    wsToggleButton_.onClick = [this] {
        auto& ws = manager_.getWebSocketServer();
        if (ws.isRunning()) {
            ws.stop();
        } else {
            ws.start(ws.getPort());
        }
        updateStatus();
    };
    addAndMakeVisible(wsToggleButton_);

    // ── HTTP section ──
    httpSectionLabel_.setFont(juce::Font(14.0f, juce::Font::bold));
    httpSectionLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(httpSectionLabel_);

    httpPortLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(httpPortLabel_);
    httpPortValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kAccentColour));
    httpPortValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(httpPortValueLabel_);

    httpStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(kTextColour));
    addAndMakeVisible(httpStatusLabel_);
    httpStatusValueLabel_.setFont(juce::Font(12.0f, juce::Font::bold));
    addAndMakeVisible(httpStatusValueLabel_);

    httpToggleButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(kSurfaceColour));
    httpToggleButton_.setColour(juce::TextButton::textColourOnId, juce::Colour(kTextColour));
    httpToggleButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(kTextColour));
    httpToggleButton_.onClick = [this] {
        auto& http = manager_.getHttpApiServer();
        if (http.isRunning()) {
            http.stop();
        } else {
            http.start(http.getPort());
        }
        updateStatus();
    };
    addAndMakeVisible(httpToggleButton_);

    // ── Info text ──
    infoLabel_.setFont(juce::Font(11.0f));
    infoLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    infoLabel_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(infoLabel_);

    updateStatus();

    // Refresh status at 2 Hz
    startTimerHz(2);
}

StreamDeckTab::~StreamDeckTab()
{
    stopTimer();
}

void StreamDeckTab::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));

    // Separator between WebSocket and HTTP sections
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH = 28;
    constexpr int gap  = 6;

    // After WS section: header + port + status + clients + button = 5 rows
    int separatorY = bounds.getY() + (rowH + gap) * 5 - gap / 2;
    g.setColour(juce::Colour(kDimTextColour).withAlpha(0.3f));
    g.drawHorizontalLine(separatorY, static_cast<float>(bounds.getX()),
                         static_cast<float>(bounds.getRight()));
}

void StreamDeckTab::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH   = 28;
    constexpr int gap    = 6;
    constexpr int labelW = 80;
    constexpr int valueW = 100;
    constexpr int btnW   = 70;

    int y = bounds.getY();

    // ── WebSocket section ──
    wsSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    wsPortLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    wsPortValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    wsStatusLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    wsStatusValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    wsClientsLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    wsClientsValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    wsToggleButton_.setBounds(bounds.getX(), y, btnW, rowH);
    y += rowH + gap + 8; // extra gap for separator

    // ── HTTP section ──
    httpSectionLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), rowH);
    y += rowH + gap;

    httpPortLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    httpPortValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    httpStatusLabel_.setBounds(bounds.getX(), y, labelW, rowH);
    httpStatusValueLabel_.setBounds(bounds.getX() + labelW + gap, y, valueW, rowH);
    y += rowH + gap;

    httpToggleButton_.setBounds(bounds.getX(), y, btnW, rowH);
    y += rowH + gap + 8;

    // Info text fills remaining space
    infoLabel_.setBounds(bounds.getX(), y, bounds.getWidth(), bounds.getBottom() - y);
}

void StreamDeckTab::timerCallback()
{
    updateStatus();
}

void StreamDeckTab::updateStatus()
{
    auto& ws = manager_.getWebSocketServer();
    auto& http = manager_.getHttpApiServer();

    // WebSocket
    wsPortValueLabel_.setText(juce::String(ws.getPort()), juce::dontSendNotification);

    if (ws.isRunning()) {
        wsStatusValueLabel_.setText("Running", juce::dontSendNotification);
        wsStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kGreenColour));
        wsToggleButton_.setButtonText("Stop");
    } else {
        wsStatusValueLabel_.setText("Stopped", juce::dontSendNotification);
        wsStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kRedColour));
        wsToggleButton_.setButtonText("Start");
    }

    wsClientsValueLabel_.setText(juce::String(ws.getClientCount()), juce::dontSendNotification);

    // HTTP
    httpPortValueLabel_.setText(juce::String(http.getPort()), juce::dontSendNotification);

    if (http.isRunning()) {
        httpStatusValueLabel_.setText("Running", juce::dontSendNotification);
        httpStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kGreenColour));
        httpToggleButton_.setButtonText("Stop");
    } else {
        httpStatusValueLabel_.setText("Stopped", juce::dontSendNotification);
        httpStatusValueLabel_.setColour(juce::Label::textColourId, juce::Colour(kRedColour));
        httpToggleButton_.setButtonText("Start");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  ControlSettingsPanel implementation (top-level tabbed container)
// ═════════════════════════════════════════════════════════════════════════════

ControlSettingsPanel::ControlSettingsPanel(ControlManager& manager)
    : manager_(manager)
{
    // Create tab content components
    hotkeyTab_     = std::make_unique<HotkeyTab>(manager_);
    midiTab_       = std::make_unique<MidiTab>(manager_);
    streamDeckTab_ = std::make_unique<StreamDeckTab>(manager_);

    // Configure the tabbed component
    tabbedComponent_.setTabBarDepth(30);
    tabbedComponent_.setOutline(0);

    // Add tabs — the tabbed component takes ownership of the colour but
    // we keep ownership of the components via unique_ptr.
    tabbedComponent_.addTab("Hotkeys",     juce::Colour(kTabBarColour), hotkeyTab_.get(),     false);
    tabbedComponent_.addTab("MIDI",        juce::Colour(kTabBarColour), midiTab_.get(),       false);
    tabbedComponent_.addTab("Stream Deck", juce::Colour(kTabBarColour), streamDeckTab_.get(), false);

    // Style the tab bar
    auto& tabBar = tabbedComponent_.getTabbedButtonBar();
    tabBar.setColour(juce::TabbedButtonBar::tabOutlineColourId,
                     juce::Colours::transparentBlack);
    tabBar.setColour(juce::TabbedButtonBar::frontOutlineColourId,
                     juce::Colour(kAccentColour));

    addAndMakeVisible(tabbedComponent_);
}

ControlSettingsPanel::~ControlSettingsPanel() = default;

void ControlSettingsPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));
}

void ControlSettingsPanel::resized()
{
    tabbedComponent_.setBounds(getLocalBounds());
}

void ControlSettingsPanel::refreshAll()
{
    hotkeyTab_->refreshBindings();
    midiTab_->refreshAll();
    // StreamDeckTab refreshes automatically via timer
}

} // namespace directpipe
