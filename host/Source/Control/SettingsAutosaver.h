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
 * @file SettingsAutosaver.h
 * @brief Dirty-flag auto-save with 1-second debounce timer.
 *
 * Owns the settings persistence lifecycle: markDirty() / tick() / saveNow().
 * tick() is called every 30Hz timer tick from MainComponent.
 */
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <functional>
#include <memory>

namespace directpipe {

class AudioEngine;
class PresetManager;

class SettingsAutosaver {
public:
    SettingsAutosaver(PresetManager& presetMgr, AudioEngine& engine,
                      std::atomic<bool>& loadingSlot,
                      std::atomic<bool>& partialLoad);
    ~SettingsAutosaver();

    /** Mark settings dirty — save will happen ~1s later. */
    void markDirty();

    /** Call every timer tick (~30Hz). Handles debounce countdown and triggers save. */
    void tick();

    /** Immediate save (called from destructor or explicit flush). */
    void saveNow();

    /** Load settings from auto-save file.
     *  Calls onPostLoad after loading (for UI refresh). */
    void loadFromFile();

    // ── Callbacks (set by MainComponent) ──

    /** Called after settings are loaded for UI refresh. */
    std::function<void()> onPostLoad;

    /** Called to show the main window after preload completes. */
    std::function<void()> onShowWindow;

    /** Restore panic mute state after loading. */
    std::function<void()> onRestorePanicMute;

    /** Flush pending logs after loading. */
    std::function<void()> onFlushLogs;

private:
    PresetManager& presetMgr_;
    AudioEngine& engine_;
    std::atomic<bool>& loadingSlot_;
    std::atomic<bool>& partialLoad_;

    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    bool dirty_ = false;
    int cooldown_ = 0;      // ticks remaining before save (at 30Hz)
    int deferCount_ = 0;    // consecutive deferred attempts (force save after limit)
    static constexpr int kMaxDeferCount = 50;  // ~15s at 300ms per retry

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsAutosaver)
};

} // namespace directpipe
