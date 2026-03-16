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
 * @file VSTChain.h
 * @brief VST plugin chain management
 *
 * Manages loading, ordering, and processing of VST2/VST3 plugins
 * in a serial chain. Uses JUCE's AudioProcessorGraph internally.
 */
#pragma once

#include <JuceHeader.h>
#include "../Util/StrongIndex.h"
#include "../ActionResult.h"
#include "BuiltinFilter.h"
#include "BuiltinNoiseRemoval.h"
#include "BuiltinAutoGain.h"
#include <vector>
#include <memory>
#include <functional>
#include <thread>

namespace directpipe {

/**
 * @brief Per-plugin latency information.
 */
struct PluginLatencyInfo {
    int latencySamples = 0;
    float latencyMs = 0.0f;
};

/**
 * @brief Information about a loaded plugin in the chain.
 */
struct PluginSlot {
    /// Built-in processor type (VST for external plugins, others for built-in processors)
    enum class Type { VST, BuiltinFilter, BuiltinNoiseRemoval, BuiltinAutoGain };

    juce::String name;
    juce::String path;           ///< fileOrIdentifier (may be shared for shell plugins)
    juce::PluginDescription desc; ///< Full description for accurate re-loading
    bool bypassed = false;
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::AudioPluginInstance* instance = nullptr;  // non-owning (VST plugins)

    Type type = Type::VST;
    juce::AudioProcessor* builtinProcessor = nullptr;  // non-owning (graph owns it), non-null for built-in types

    /// Unified accessor -- returns whichever processor is active (built-in or VST)
    juce::AudioProcessor* getProcessor() const {
        if (type != Type::VST && builtinProcessor)
            return builtinProcessor;
        return instance;
    }
};

/**
 * @brief VST plugin chain processor.
 *
 * Manages a serial chain of VST plugins:
 * Input → Plugin1 → Plugin2 → ... → PluginN → Output
 *
 * All plugin processing is inline (zero additional latency).
 */
class VSTChain {
public:
    VSTChain();
    ~VSTChain();

    /**
     * @brief Prepare the chain for playback.
     * @param sampleRate Audio sample rate.
     * @param blockSize Maximum expected block size.
     */
    void prepareToPlay(double sampleRate, int blockSize);

    /**
     * @brief Release resources when playback stops.
     */
    void releaseResources();

    /**
     * @brief Process an audio buffer through the VST chain.
     *
     * This is called from the real-time audio thread.
     * No allocations, no locks.
     *
     * @param buffer Audio buffer to process in-place.
     * @param numSamples Number of samples to process.
     */
    void processBlock(juce::AudioBuffer<float>& buffer, int numSamples);  // [RT thread — NO chainLock_, NO allocation]

    /**
     * @brief Scan system for available VST plugins.
     * @param directoriesToScan List of directories to scan for VSTs.
     */
    void scanForPlugins(const juce::StringArray& directoriesToScan);

    /**
     * @brief Add a plugin to the end of the chain.
     * @param pluginPath Full path to the VST plugin file.
     * @return Index of the added plugin, or -1 on failure.
     */
    int addPlugin(const juce::String& pluginPath);  // [Message thread — holds chainLock_]

    /**
     * @brief Add a plugin from a full PluginDescription (preferred).
     * @param desc Plugin description from scanner.
     * @return Index of the added plugin, or -1 on failure.
     */
    int addPlugin(const juce::PluginDescription& desc);  // [Message thread — holds chainLock_]

    /**
     * @brief Add a built-in processor to the chain.
     * @param type The built-in processor type (must not be VST).
     * @param insertIndex Position to insert at (-1 = append to end).
     * @return ActionResult ok/fail.
     */
    [[nodiscard]] ActionResult addBuiltinProcessor(PluginSlot::Type type, int insertIndex = -1);  // [Message thread — holds chainLock_]

    /**
     * @brief Add Filter + NoiseRemoval + AutoGain (Auto button).
     *
     * Inserts at the front of the chain. Skips types that are already present.
     * @return ActionResult ok/fail.
     */
    [[nodiscard]] ActionResult addAutoProcessors();  // [Message thread — holds chainLock_]

    /**
     * @brief Remove a plugin from the chain.
     * @param index Position in the chain.
     * @return true if removed successfully.
     */
    bool removePlugin(int index);  // [Message thread — holds chainLock_]

    /**
     * @brief Move a plugin to a new position in the chain.
     * @param from Current position (PluginIndex for type safety).
     * @param to Desired position (PluginIndex for type safety).
     * @return true if moved successfully.
     */
    bool movePlugin(PluginIndex from, PluginIndex to);  // [Message thread — holds chainLock_]

    /**
     * @brief Toggle bypass for a plugin.
     * @param index Position in the chain.
     * @param bypassed true to bypass.
     */
    void setPluginBypassed(int index, bool bypassed);

    /**
     * @brief Toggle bypass state (read under lock, then call setPluginBypassed).
     * @param index Position in the chain.
     */
    void togglePluginBypassed(int index);

    /**
     * @brief Check if a plugin is bypassed (safe single-lock read).
     */
    bool isPluginBypassed(int index) const;

    /**
     * @brief Get the number of plugins in the chain.
     */
    int getPluginCount() const;

    /** Get per-plugin latency info. [Message thread — acquires chainLock_] */
    std::vector<PluginLatencyInfo> getPluginLatencies() const;

    /** Get total chain PDC from AudioProcessorGraph. [Message thread — acquires chainLock_] */
    int getTotalChainPDC() const;

    /**
     * @brief Get plugin slot info at the given index.
     */
    const PluginSlot* getPluginSlot(int index) const;

    /** @brief Get parameter count for a plugin. */
    int getPluginParameterCount(int pluginIndex) const;

    /** @brief Get parameter name. */
    juce::String getPluginParameterName(int pluginIndex, int paramIndex) const;

    /** @brief Set a plugin parameter value (0.0-1.0 normalized). */
    void setPluginParameter(int pluginIndex, int paramIndex, float value);

    /** @brief Get a plugin parameter value (0.0-1.0 normalized). */
    float getPluginParameter(int pluginIndex, int paramIndex) const;

    /**
     * @brief Get the list of known (scanned) plugin descriptions.
     */
    const juce::KnownPluginList& getKnownPlugins() const { return knownPlugins_; }

    /**
     * @brief Get the format manager (for scanner access).
     */
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager_; }

    /**
     * @brief Open the native editor window for a plugin.
     * @param index Plugin chain index.
     * @param parentComponent Parent component for the editor window.
     */
    void openPluginEditor(int index, juce::Component* parentComponent);

    /**
     * @brief Close the editor window for a plugin.
     * @param index Plugin chain index.
     */
    void closePluginEditor(int index);

    /**
     * @brief Request for async plugin loading.
     */
    struct PluginLoadRequest {
        juce::PluginDescription desc;
        juce::String name, path;
        bool bypassed = false;
        juce::MemoryBlock stateData;
        bool hasState = false;
    };

    /**
     * @brief Replace the entire chain asynchronously (non-blocking).
     *
     * Clears current chain immediately (audio goes silent), then loads
     * new plugins on a background thread. Wires them into the graph
     * on the message thread via callAsync when done.
     * @param requests Plugins to load.
     * @param onComplete Called on message thread when loading finishes.
     */
    // [Message → BG → callAsync → Message, alive_ guard + asyncGeneration_ counter]
    void replaceChainAsync(std::vector<PluginLoadRequest> requests,
                           std::function<void()> onComplete,
                           std::function<void()> preWork = nullptr);

    /**
     * @brief A pre-loaded plugin instance ready for graph insertion.
     */
    struct PreloadedPlugin {
        std::unique_ptr<juce::AudioPluginInstance> instance;
        PluginLoadRequest request;
    };

    /**
     * @brief Replace the entire chain with pre-loaded instances (synchronous).
     *
     * Must be called on the message thread. Used with PluginPreloadCache
     * to skip DLL loading entirely. Old chain continues processing until
     * swap completes (~10-50ms suspend).
     * @param preloaded Pre-created plugin instances with metadata.
     * @param onComplete Called after swap is complete.
     */
    void replaceChainWithPreloaded(std::vector<PreloadedPlugin> preloaded,
                                   std::function<void()> onComplete);

    /** @brief True while async chain loading is in progress. */
    bool isLoading() const { return asyncLoading_.load(); }

    /** @brief Suspend/resume graph processing (for safe state changes). */
    void suspendProcessing(bool suspend) { graph_->suspendProcessing(suspend); }

    // Callback when the chain changes (for UI update)
    std::function<void()> onChainChanged;

    // Callback when a plugin editor window is closed (for state save)
    std::function<void()> onEditorClosed;

    // Callback when a plugin fails to load (name, error message)
    std::function<void(const juce::String&, const juce::String&)> onPluginLoadFailed;

private:
    /**
     * @brief Rebuild the audio graph connections after chain modification.
     * @param suspend If true, suspend/resume processing around rebuild
     *        (needed when nodes are added/removed). If false, only connections
     *        change and the render sequence swaps atomically (no audio gap).
     */
    void rebuildGraph(bool suspend = true);

    /**
     * @brief Load a VST plugin from a description.
     */
    std::unique_ptr<juce::AudioPluginInstance> loadPlugin(
        const juce::PluginDescription& desc, juce::String& error);

    // ═══════════════════════════════════════════════════════════════════
    // Thread Ownership — 변경 시 Audio/README.md "Thread Model" 테이블도 업데이트할 것
    // ═══════════════════════════════════════════════════════════════════

    juce::AudioPluginFormatManager formatManager_;       // [Message thread only]
    juce::KnownPluginList knownPlugins_;                 // [Message thread only]
    std::unique_ptr<juce::AudioProcessorGraph> graph_;   // [RT: processBlock, Message: node add/remove]

    // I/O nodes in the graph
    juce::AudioProcessorGraph::NodeID inputNodeId_;      // [Protected by chainLock_]
    juce::AudioProcessorGraph::NodeID outputNodeId_;     // [Protected by chainLock_]

    // ─── Protected by chainLock_ ───
    std::vector<PluginSlot> chain_;                      // [Protected by chainLock_]
    std::vector<std::unique_ptr<juce::DocumentWindow>> editorWindows_;  // [Protected by chainLock_]

    double currentSampleRate_ = 48000.0;                 // [Message thread only]
    int currentBlockSize_ = 128;                         // [Message thread only]
    std::atomic<bool> prepared_{false};                   // [Message write, RT read]

    juce::MidiBuffer emptyMidi_;                         // [RT thread only] Pre-allocated (avoids per-callback allocation)

    std::atomic<bool> chainDirty_{false};                // [Message write, RT read] Lock-free chain swap flag

    // [Protected: chain_, editorWindows_, graph nodes. NEVER in processBlock. NEVER writeToLog inside.]
    mutable juce::CriticalSection chainLock_;

    // ─── Async loading state ───
    std::atomic<bool> asyncLoading_{false};               // [BG write, Message/UI read]
    std::unique_ptr<std::thread> loadThread_;             // [Message thread only]
    std::atomic<uint32_t> asyncGeneration_{0};            // [Message write, BG read] Incremented per replaceChainAsync call

    // [callAsync lifetime guard — shared_ptr captured by value in lambda, checked before accessing this]
    std::shared_ptr<std::atomic<bool>> alive_ = std::make_shared<std::atomic<bool>>(true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VSTChain)
};

} // namespace directpipe
