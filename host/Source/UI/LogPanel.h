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
 * @file LogPanel.h
 * @brief Real-time log viewer with export and maintenance (clear) actions
 */
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>

namespace directpipe {

/**
 * @brief Lock-free logger sink that captures juce::Logger output
 *        from any thread into a ring buffer drained on the message thread.
 */
class DirectPipeLogger : public juce::Logger {
public:
    DirectPipeLogger();
    ~DirectPipeLogger() override;

    void logMessage(const juce::String& message) override;

    /** Drain pending entries. Call from message thread only. Returns count drained. */
    int drain(juce::StringArray& out);

    /** Discard all pending entries. */
    void clearPending();

private:
    static constexpr int kMaxPending = 512;
    juce::String pendingBuf_[kMaxPending];
    std::atomic<uint32_t> writeIdx_{0};
    std::atomic<uint32_t> readIdx_{0};
    juce::Logger* previousLogger_ = nullptr;
};

/**
 * @brief Log viewer tab — real-time log display, export, and maintenance actions.
 */
class LogPanel : public juce::Component {
public:
    LogPanel();
    ~LogPanel() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Call from MainComponent::timerCallback() at 30Hz. */
    void flushPendingLogs();

    /** Wired by MainComponent — called after Reset Settings deletes config files. */
    std::function<void()> onResetSettings;

private:
    // Log display
    juce::TextEditor logView_;
    juce::TextButton exportBtn_{"Export Log"};
    juce::TextButton clearLogBtn_{"Clear Log"};

    // Maintenance section
    juce::Label maintenanceLabel_{"", "Maintenance"};
    juce::TextButton clearPluginCacheBtn_{"Clear Plugin Cache"};
    juce::TextButton clearPresetsBtn_{"Clear All Presets"};
    juce::TextButton resetSettingsBtn_{"Reset Settings"};

    // In-memory log history
    juce::StringArray logLines_;
    static constexpr int kMaxLogLines = 1000;

    // Logger sink
    DirectPipeLogger logger_;

    // File chooser (prevent dangling async chooser)
    std::shared_ptr<juce::FileChooser> fileChooser_;

    // Actions
    void onExportLog();
    void onClearLog();
    void onClearPluginCache();
    void onClearAllPresets();
    void onResetSettingsClicked();

    void appendLine(const juce::String& line);

    static juce::File getConfigDir();

    // Theme
    static constexpr juce::uint32 kBgColour      = 0xFF1E1E2E;
    static constexpr juce::uint32 kSurfaceColour  = 0xFF2A2A40;
    static constexpr juce::uint32 kAccentColour   = 0xFF6C63FF;
    static constexpr juce::uint32 kTextColour     = 0xFFE0E0E0;
    static constexpr juce::uint32 kDimTextColour  = 0xFF8888AA;
    static constexpr juce::uint32 kRedColour      = 0xFFE05050;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LogPanel)
};

} // namespace directpipe
