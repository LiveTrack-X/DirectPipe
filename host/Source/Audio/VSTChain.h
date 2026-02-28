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
#include <vector>
#include <memory>
#include <functional>
#include <thread>

namespace directpipe {

/**
 * @brief Information about a loaded plugin in the chain.
 */
struct PluginSlot {
    juce::String name;
    juce::String path;           ///< fileOrIdentifier (may be shared for shell plugins)
    juce::PluginDescription desc; ///< Full description for accurate re-loading
    bool bypassed = false;
    juce::AudioProcessorGraph::NodeID nodeId;
    juce::AudioPluginInstance* instance = nullptr;  // non-owning
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
    void processBlock(juce::AudioBuffer<float>& buffer, int numSamples);

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
    int addPlugin(const juce::String& pluginPath);

    /**
     * @brief Add a plugin from a full PluginDescription (preferred).
     * @param desc Plugin description from scanner.
     * @return Index of the added plugin, or -1 on failure.
     */
    int addPlugin(const juce::PluginDescription& desc);

    /**
     * @brief Remove a plugin from the chain.
     * @param index Position in the chain.
     * @return true if removed successfully.
     */
    bool removePlugin(int index);

    /**
     * @brief Move a plugin to a new position in the chain.
     * @param fromIndex Current position.
     * @param toIndex Desired position.
     * @return true if moved successfully.
     */
    bool movePlugin(int fromIndex, int toIndex);

    /**
     * @brief Toggle bypass for a plugin.
     * @param index Position in the chain.
     * @param bypassed true to bypass.
     */
    void setPluginBypassed(int index, bool bypassed);

    /**
     * @brief Get the number of plugins in the chain.
     */
    int getPluginCount() const;

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
     * Clears the current chain immediately, loads plugins on a background
     * thread, then wires them into the graph on the message thread.
     * @param requests Plugins to load.
     * @param onComplete Called on message thread when loading finishes.
     */
    void replaceChainAsync(std::vector<PluginLoadRequest> requests,
                           std::function<void()> onComplete);

    /** @brief True while async chain loading is in progress. */
    bool isLoading() const { return asyncLoading_.load(); }

    // Callback when the chain changes (for UI update)
    std::function<void()> onChainChanged;

    // Callback when a plugin editor window is closed (for state save)
    std::function<void()> onEditorClosed;

    // Callback when a plugin fails to load (name, error message)
    std::function<void(const juce::String&, const juce::String&)> onPluginLoadFailed;

private:
    /**
     * @brief Rebuild the audio graph connections after chain modification.
     */
    void rebuildGraph();

    /**
     * @brief Load a VST plugin from a description.
     */
    std::unique_ptr<juce::AudioPluginInstance> loadPlugin(
        const juce::PluginDescription& desc, juce::String& error);

    juce::AudioPluginFormatManager formatManager_;
    juce::KnownPluginList knownPlugins_;
    std::unique_ptr<juce::AudioProcessorGraph> graph_;

    // I/O nodes in the graph
    juce::AudioProcessorGraph::NodeID inputNodeId_;
    juce::AudioProcessorGraph::NodeID outputNodeId_;

    // Ordered chain of plugin slots
    std::vector<PluginSlot> chain_;

    // Editor windows (owned)
    std::vector<std::unique_ptr<juce::DocumentWindow>> editorWindows_;

    double currentSampleRate_ = 48000.0;
    int currentBlockSize_ = 128;
    bool prepared_ = false;

    // Pre-allocated MidiBuffer for processBlock (avoids per-callback allocation)
    juce::MidiBuffer emptyMidi_;

    // Atomic flag for lock-free chain swap between RT and non-RT threads
    std::atomic<bool> chainDirty_{false};

    // Mutex for chain modification (NOT used in processBlock)
    juce::CriticalSection chainLock_;

    // Async loading state
    std::atomic<bool> asyncLoading_{false};
    std::unique_ptr<std::thread> loadThread_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VSTChain)
};

} // namespace directpipe
