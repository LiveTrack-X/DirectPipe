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

namespace directpipe {

/**
 * @brief Information about a loaded plugin in the chain.
 */
struct PluginSlot {
    juce::String name;
    juce::String path;
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

    /**
     * @brief Get the list of known (scanned) plugin descriptions.
     */
    const juce::KnownPluginList& getKnownPlugins() const { return knownPlugins_; }

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

    // Callback when the chain changes (for UI update)
    std::function<void()> onChainChanged;

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

    // Mutex for chain modification (NOT used in processBlock)
    juce::CriticalSection chainLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VSTChain)
};

} // namespace directpipe
