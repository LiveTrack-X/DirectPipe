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
 * @file SettingsAutosaver.cpp
 * @brief Dirty-flag auto-save implementation
 */

#include "SettingsAutosaver.h"
#include "../Audio/AudioEngine.h"
#include "../UI/PresetManager.h"

namespace directpipe {
namespace {
bool presetDeclaresOutputMuted(const juce::File& file)
{
    auto parsed = juce::JSON::parse(file.loadFileAsString());
    if (auto* root = parsed.getDynamicObject())
        return root->hasProperty("outputMuted");
    return false;
}
} // namespace

SettingsAutosaver::SettingsAutosaver(PresetManager& presetMgr, AudioEngine& engine,
                                     std::atomic<bool>& loadingSlot,
                                     std::atomic<bool>& partialLoad)
    : presetMgr_(presetMgr),
      engine_(engine),
      loadingSlot_(loadingSlot),
      partialLoad_(partialLoad)
{
}

SettingsAutosaver::~SettingsAutosaver()
{
    alive_->store(false);
}

void SettingsAutosaver::markDirty()
{
    dirty_ = true;
    cooldown_ = 30;  // reset debounce: save after ~1 second of inactivity
}

void SettingsAutosaver::tick()
{
    if (!dirty_ || cooldown_ <= 0) return;

    if (--cooldown_ == 0) {
        // Defer save if chain is in transitional state (async loading or not yet prepared)
        if (loadingSlot_.load() || !engine_.getVSTChain().isStable()) {
            if (++deferCount_ >= kMaxDeferCount) {
                // Force save after ~15s of deferred attempts to prevent data loss
                juce::Logger::writeToLog("[PRESET] Autosave forced after " + juce::String(kMaxDeferCount) + " deferred attempts");
                deferCount_ = 0;
                dirty_ = false;
                saveNow();
            } else {
                cooldown_ = 10;  // retry in ~300ms
            }
        } else {
            deferCount_ = 0;
            dirty_ = false;
            saveNow();
        }
    }
}

void SettingsAutosaver::saveNow()
{
    // Skip saving during async chain load or before chain is prepared:
    // chain is in transitional state (empty or partially loaded).
    // Saving now would corrupt the active slot file.
    if (loadingSlot_.load() || !engine_.getVSTChain().isStable())
        return;

    // Save current slot's chain state (captures plugin internal state)
    // Skip slot save if partial load (some plugins failed): preserve original slot file
    int currentSlot = presetMgr_.getActiveSlot();
    if (currentSlot >= 0 && !partialLoad_.load())
        presetMgr_.saveSlot(currentSlot);

    auto file = PresetManager::getAutoSaveFile();
    presetMgr_.savePreset(file);
}

void SettingsAutosaver::loadFromFile()
{
    if (!engine_.isOutputNone())
        engine_.setOutputMuted(true); // Startup guard: prevent transient default-driver output.

    auto file = PresetManager::getAutoSaveFile();
    if (file.existsAsFile()) {
        loadingSlot_ = true;
        const bool loaded = presetMgr_.loadPreset(file);
        const bool hasOutputMutedField = loaded && presetDeclaresOutputMuted(file);

        // Self-healing: if settings.dppreset had an empty/corrupt chain but the
        // active slot file is valid, reload chain from the slot file.
        int activeSlot = presetMgr_.getActiveSlot();
        if (activeSlot >= 0 && presetMgr_.isSlotOccupied(activeSlot)
            && engine_.getVSTChain().getPluginCount() == 0) {
            juce::Logger::writeToLog("[PRESET] Self-healing: reloading slot "
                + juce::String::charToString(PresetManager::slotLabel(activeSlot)) + " from file");
            bool ok = presetMgr_.loadSlot(activeSlot);
            partialLoad_ = !ok;  // clear on success, set on failure
        }

        loadingSlot_ = false;

        // Legacy preset may omit outputMuted; use startup default (unmuted).
        if ((!loaded || !hasOutputMutedField) && !engine_.isOutputNone())
            engine_.setOutputMuted(false);

        // Restore panic mute lockout (monitor/IPC disabled while muted)
        if (onRestorePanicMute) onRestorePanicMute();

        if (onPostLoad) onPostLoad();
        if (onFlushLogs) onFlushLogs();

        // Pre-load other slots in background for instant switching.
        // Deferred via callAsync to ensure audio device is fully started
        // before preload thread uses formatManager.
        // Window is shown AFTER preload completes (prevents "frozen UI" appearance).
        auto showWindowCb = onShowWindow;
        auto* pm = &presetMgr_;
        auto aliveFlag = alive_;
        juce::MessageManager::callAsync([pm, showWindowCb, aliveFlag]() {
            if (!aliveFlag->load()) return;
            pm->triggerPreload([showWindowCb, aliveFlag]() {
                if (aliveFlag->load() && showWindowCb) showWindowCb();
            });
        });
        return;  // window will be shown by preload callback
    }

    if (!engine_.isOutputNone())
        engine_.setOutputMuted(false);

    // No settings file: show window immediately
    auto showWindowCb = onShowWindow;
    juce::MessageManager::callAsync([showWindowCb]() {
        if (showWindowCb) showWindowCb();
    });
}

} // namespace directpipe
