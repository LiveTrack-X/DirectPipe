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
 * @file LogPanel.cpp
 * @brief Real-time log viewer with export and maintenance actions
 */

#include "LogPanel.h"
#include "../Control/ControlMapping.h"

// Forward declarations (defined in Main.cpp)
bool isStartupEnabled();
void setStartupEnabled(bool enable);

namespace directpipe {

// ═════════════════════════════════════════════════════════════
//  DirectPipeLogger
// ═════════════════════════════════════════════════════════════

DirectPipeLogger::DirectPipeLogger()
{
    previousLogger_ = juce::Logger::getCurrentLogger();
    juce::Logger::setCurrentLogger(this);
}

DirectPipeLogger::~DirectPipeLogger()
{
    juce::Logger::setCurrentLogger(previousLogger_);
}

void DirectPipeLogger::logMessage(const juce::String& message)
{
    // Format with timestamp
    auto now = juce::Time::getCurrentTime();
    auto ts = now.toString(false, true, true, true);  // HH:MM:SS.mmm
    auto line = "[" + ts + "] " + message;

    // Mutex protects multi-producer writes (called from WebSocket, HTTP, audio, MIDI threads)
    std::lock_guard<std::mutex> lock(writeMutex_);

    uint32_t w = writeIdx_.load(std::memory_order_relaxed);
    uint32_t r = readIdx_.load(std::memory_order_acquire);

    // Drop if buffer full
    if (w - r >= static_cast<uint32_t>(kMaxPending))
        return;

    pendingBuf_[w % static_cast<uint32_t>(kMaxPending)] = line;
    writeIdx_.store(w + 1, std::memory_order_release);
}

int DirectPipeLogger::drain(juce::StringArray& out)
{
    uint32_t r = readIdx_.load(std::memory_order_relaxed);
    uint32_t w = writeIdx_.load(std::memory_order_acquire);
    int count = 0;

    while (r != w) {
        out.add(pendingBuf_[r % static_cast<uint32_t>(kMaxPending)]);
        ++r;
        ++count;
    }

    readIdx_.store(r, std::memory_order_release);
    return count;
}

void DirectPipeLogger::clearPending()
{
    readIdx_.store(writeIdx_.load(std::memory_order_relaxed),
                   std::memory_order_relaxed);
}

// ═════════════════════════════════════════════════════════════
//  LogPanel
// ═════════════════════════════════════════════════════════════

LogPanel::LogPanel()
{
    // ── Application section ──
    appHeaderLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
    appHeaderLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(appHeaderLabel_);

    startupToggle_.setColour(juce::ToggleButton::textColourId, juce::Colour(kTextColour));
    startupToggle_.setColour(juce::ToggleButton::tickColourId, juce::Colour(kAccentColour));
#if JUCE_WINDOWS
    startupToggle_.setToggleState(::isStartupEnabled(), juce::dontSendNotification);
    startupToggle_.onClick = [this] {
        ::setStartupEnabled(startupToggle_.getToggleState());
    };
#endif
    addAndMakeVisible(startupToggle_);

    // ── Settings Export/Import section ──
    settingsHeaderLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
    settingsHeaderLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(settingsHeaderLabel_);

    // Log view — read-only multiline with monospaced font
    logView_.setMultiLine(true, true);
    logView_.setReadOnly(true);
    logView_.setScrollbarsShown(true);
    logView_.setCaretVisible(false);
    logView_.setColour(juce::TextEditor::backgroundColourId,  juce::Colour(kSurfaceColour));
    logView_.setColour(juce::TextEditor::textColourId,        juce::Colour(kTextColour));
    logView_.setColour(juce::TextEditor::outlineColourId,     juce::Colours::transparentBlack);
    logView_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(kAccentColour));
    logView_.setFont(juce::Font(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::plain));
    addAndMakeVisible(logView_);

    // Export / Clear Log buttons
    auto setupBtn = [this](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId,  juce::Colour(kSurfaceColour));
        btn.setColour(juce::TextButton::textColourOnId,  juce::Colours::white);
        btn.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        addAndMakeVisible(btn);
    };

    setupBtn(saveSettingsBtn_);
    setupBtn(loadSettingsBtn_);

    saveSettingsBtn_.onClick = [this] { if (onSaveSettings) onSaveSettings(); };
    loadSettingsBtn_.onClick = [this] { if (onLoadSettings) onLoadSettings(); };

    // Log header
    logHeaderLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
    logHeaderLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(logHeaderLabel_);

    setupBtn(exportBtn_);
    setupBtn(clearLogBtn_);

    exportBtn_.onClick   = [this] { onExportLog(); };
    clearLogBtn_.onClick = [this] { onClearLog(); };

    // Maintenance section header
    maintenanceLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
    maintenanceLabel_.setColour(juce::Label::textColourId, juce::Colour(kDimTextColour));
    addAndMakeVisible(maintenanceLabel_);

    // Maintenance buttons
    setupBtn(clearPluginCacheBtn_);
    setupBtn(clearPresetsBtn_);
    setupBtn(resetSettingsBtn_);

    // Reset button with red tint
    resetSettingsBtn_.setColour(juce::TextButton::buttonColourId, juce::Colour(kRedColour).withAlpha(0.3f));

    clearPluginCacheBtn_.onClick = [this] { onClearPluginCache(); };
    clearPresetsBtn_.onClick     = [this] { onClearAllPresets(); };
    resetSettingsBtn_.onClick    = [this] { onResetSettingsClicked(); };
}

void LogPanel::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(kBgColour));
}

void LogPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);
    constexpr int rowH = 28;
    constexpr int gap  = 6;
    constexpr int headerH = 18;

    // Fixed sections height (bottom):
    int x = bounds.getX();
    int w = bounds.getWidth();
    int y = bounds.getY();

    // ── Application section ──
    appHeaderLabel_.setBounds(x, y, w, headerH);
    y += headerH + gap;

    startupToggle_.setBounds(x, y, w, rowH);
    y += rowH + gap;

    // ── Settings Export/Import section ──
    settingsHeaderLabel_.setBounds(x, y, w, headerH);
    y += headerH + gap;

    int btnW = (w - gap) / 2;
    saveSettingsBtn_.setBounds(x, y, btnW, rowH);
    loadSettingsBtn_.setBounds(x + btnW + gap, y, w - btnW - gap, rowH);
    y += rowH + gap;

    // ── Log section ──
    logHeaderLabel_.setBounds(x, y, w, headerH);
    y += headerH + gap;

    // Bottom sections: Export/Clear + Maintenance
    constexpr int bottomH = rowH + gap * 2       // export/clear row + gap
                          + headerH + gap         // maintenance header
                          + rowH * 3 + gap * 2;  // 3 maintenance buttons

    int logH = bounds.getBottom() - y - gap - bottomH;
    if (logH < 60) logH = 60;
    logView_.setBounds(x, y, w, logH);
    y += logH + gap;

    // Export / Clear Log row
    exportBtn_.setBounds(x, y, btnW, rowH);
    clearLogBtn_.setBounds(x + btnW + gap, y, w - btnW - gap, rowH);
    y += rowH + gap * 2;

    // ── Maintenance section ──
    maintenanceLabel_.setBounds(x, y, w, headerH);
    y += headerH + gap;

    clearPluginCacheBtn_.setBounds(x, y, w, rowH);
    y += rowH + gap;

    clearPresetsBtn_.setBounds(x, y, w, rowH);
    y += rowH + gap;

    resetSettingsBtn_.setBounds(x, y, w, rowH);
}

void LogPanel::flushPendingLogs()
{
    juce::StringArray newLines;
    int count = logger_.drain(newLines);
    if (count <= 0) return;

    // Batch add to logLines_
    for (const auto& line : newLines)
        logLines_.add(line);

    // Batch trim excess
    int excess = logLines_.size() - kMaxLogLines;
    if (excess > 0)
        logLines_.removeRange(0, excess);

    // Batch update TextEditor (single operation instead of per-line)
    juce::String batch;
    for (const auto& line : newLines)
        batch << line << "\n";

    logView_.moveCaretToEnd();
    logView_.insertTextAtCaret(batch);

    // Trim TextEditor front if over limit
    if (excess > 0) {
        auto fullText = logView_.getText();
        int removeUpTo = 0;
        for (int i = 0; i < excess; ++i) {
            int nl = fullText.indexOf(removeUpTo, "\n");
            if (nl >= 0) removeUpTo = nl + 1;
            else break;
        }
        if (removeUpTo > 0) {
            logView_.setHighlightedRegion({0, removeUpTo});
            logView_.insertTextAtCaret("");
        }
    }
}

void LogPanel::appendLine(const juce::String& line)
{
    logLines_.add(line);

    // Enforce line cap
    if (logLines_.size() > kMaxLogLines) {
        int excess = logLines_.size() - kMaxLogLines;
        logLines_.removeRange(0, excess);

        // Remove from the TextEditor front instead of full rebuild
        auto fullText = logView_.getText();
        int removeUpTo = 0;
        for (int i = 0; i < excess; ++i) {
            int nl = fullText.indexOf(removeUpTo, "\n");
            if (nl >= 0) removeUpTo = nl + 1;
            else break;
        }
        if (removeUpTo > 0) {
            logView_.setHighlightedRegion({0, removeUpTo});
            logView_.insertTextAtCaret("");
        }
        logView_.moveCaretToEnd();
        logView_.insertTextAtCaret(line + "\n");
    } else {
        logView_.moveCaretToEnd();
        logView_.insertTextAtCaret(line + "\n");
    }
}

void LogPanel::onExportLog()
{
    auto timestamp = juce::Time::getCurrentTime().formatted("%Y-%m-%d");
    auto defaultFile = juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
        .getChildFile("DirectPipe-log-" + timestamp + ".txt");

    fileChooser_ = std::make_shared<juce::FileChooser>(
        "Export Log", defaultFile, "*.txt");

    auto safeThis = juce::Component::SafePointer<LogPanel>(this);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& fc) {
            if (!safeThis) return;
            auto file = fc.getResult();
            if (file == juce::File())
                return;
            file.replaceWithText(safeThis->logLines_.joinIntoString("\n") + "\n");
            juce::Logger::writeToLog("[APP] Log exported to " + file.getFullPathName());
        });
}

void LogPanel::onClearLog()
{
    logLines_.clear();
    logView_.clear();
    logger_.clearPending();
}

void LogPanel::onClearPluginCache()
{
    bool ok = juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        "Clear Plugin Cache",
        "This will delete the scanned plugin list.\n"
        "DirectPipe will need to re-scan all plugins.\n\nContinue?",
        "OK", "Cancel", nullptr, nullptr);

    if (!ok) return;

    auto dir = getConfigDir();
    dir.getChildFile("plugin-cache.xml").deleteFile();
    dir.getChildFile("scan-result.xml").deleteFile();
    dir.getChildFile("scan-deadmanspedal.txt").deleteFile();
    dir.getChildFile("scan-blacklist.txt").deleteFile();

    juce::Logger::writeToLog("[APP] Plugin cache cleared");
}

void LogPanel::onClearAllPresets()
{
    bool ok = juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        "Clear All Presets",
        "This will delete all 5 quick slot presets (A-E)\n"
        "and all saved user presets.\n\nThis cannot be undone. Continue?",
        "OK", "Cancel", nullptr, nullptr);

    if (!ok) return;

    auto dir = getConfigDir();

    // Quick slots A-E
    auto slotsDir = dir.getChildFile("Slots");
    for (int i = 0; i < 5; ++i) {
        char label = static_cast<char>('A' + i);
        slotsDir.getChildFile(juce::String("slot_") + juce::String::charToString(label) + ".dppreset").deleteFile();
    }

    // User presets
    auto presetsDir = dir.getChildFile("Presets");
    auto presetFiles = presetsDir.findChildFiles(
        juce::File::findFiles, false, "*.dppreset");
    for (auto& f : presetFiles)
        f.deleteFile();

    juce::Logger::writeToLog("[APP] All presets cleared");
}

void LogPanel::onResetSettingsClicked()
{
    bool ok = juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::WarningIcon,
        "Reset Settings",
        "This will delete all audio settings, hotkeys,\n"
        "MIDI mappings, and server config.\n\n"
        "DirectPipe will reload with factory defaults.\n\nContinue?",
        "OK", "Cancel", nullptr, nullptr);

    if (!ok) return;

    auto dir = getConfigDir();
    dir.getChildFile("settings.dppreset").deleteFile();
    dir.getChildFile("directpipe-controls.json").deleteFile();
    dir.getChildFile("recording-config.json").deleteFile();

    juce::Logger::writeToLog("[APP] Settings reset to factory defaults");

    if (onResetSettings)
        onResetSettings();
}

juce::File LogPanel::getConfigDir()
{
    return ControlMappingStore::getConfigDirectory();
}

} // namespace directpipe
