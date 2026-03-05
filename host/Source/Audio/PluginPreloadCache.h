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
 * @file PluginPreloadCache.h
 * @brief Background pre-loading of plugin instances for instant preset switching
 *
 * After a slot is loaded, this cache pre-loads plugin instances for other
 * occupied slots in the background. When the user switches to a cached slot,
 * the pre-loaded instances are used directly (no DLL loading delay).
 */
#pragma once

#include <JuceHeader.h>
#include <map>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <vector>

namespace directpipe {

class PluginPreloadCache {
public:
    struct CachedEntry {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        juce::PluginDescription desc;
        juce::String name;
        juce::String path;
        bool bypassed = false;
        juce::MemoryBlock stateData;
        bool hasState = false;
    };

    struct CachedSlot {
        std::vector<CachedEntry> entries;
        double sampleRate = 0;
        int blockSize = 0;
    };

    PluginPreloadCache() = default;
    ~PluginPreloadCache();

    /**
     * @brief Take cached slot data (transfers ownership).
     * @return The cached slot or nullptr if not cached / SR mismatch.
     */
    std::unique_ptr<CachedSlot> take(int slotIndex, double currentSR, int currentBS);

    /**
     * @brief Check if a slot is cached and valid.
     */
    bool isCached(int slotIndex, double currentSR, int currentBS);

    /**
     * @brief Start pre-loading all occupied slots except the given one.
     * Runs on a background thread. Cancels any previous preload.
     * @param exceptSlot Slot to skip (currently active).
     * @param sr Current sample rate.
     * @param bs Current block size.
     * @param formatMgr Plugin format manager for createPluginInstance.
     * @param knownPlugins Known plugin list for description lookup.
     * @param slotFileReader Function that reads a slot file and returns JSON string.
     */
    void preloadAllSlots(int exceptSlot, double sr, int bs,
                         juce::AudioPluginFormatManager& formatMgr,
                         const juce::KnownPluginList& knownPlugins,
                         std::function<juce::String(int)> slotFileReader,
                         std::function<void()> onComplete = nullptr);

    /** @brief Invalidate a specific slot's cache. */
    void invalidateSlot(int slotIndex);

    /** @brief Invalidate all cached slots. */
    void invalidateAll();

    /** @brief Cancel any running preload and wait for it to finish. */
    void cancelAndWait();

    /** @brief Request cancel (non-blocking). Thread will stop at next check. */
    void requestCancel() { cancelPreload_.store(true); }

    /**
     * @brief Wait for preload thread to finish (blocking).
     * Call from background thread only to avoid blocking message thread.
     */
    void joinPreloadThread();

    static constexpr int kNumSlots = 5;

private:
    std::map<int, std::unique_ptr<CachedSlot>> cache_;
    std::mutex cacheMutex_;
    std::mutex threadMutex_;  ///< Protects preloadThread_ access
    std::unique_ptr<std::thread> preloadThread_;
    std::atomic<bool> cancelPreload_{false};
    std::atomic<uint32_t> preloadGeneration_{0};  ///< Generation counter for superseding old preloads
};

} // namespace directpipe
