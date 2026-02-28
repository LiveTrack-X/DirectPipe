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
 * @file VSTChain.cpp
 * @brief VST plugin chain implementation
 */

#include "VSTChain.h"

namespace directpipe {

// ─── Plugin Editor Window ───────────────────────────────────────
// Custom DocumentWindow that properly handles the close button.

class PluginEditorWindow : public juce::DocumentWindow {
public:
    PluginEditorWindow(const juce::String& name)
        : DocumentWindow(name, juce::Colours::darkgrey,
                         DocumentWindow::closeButton | DocumentWindow::minimiseButton)
    {
        setUsingNativeTitleBar(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
        if (onClosed) onClosed();
    }

    std::function<void()> onClosed;
};

VSTChain::VSTChain()
{
    // Register standard plugin formats (VST2, VST3)
    formatManager_.addDefaultFormats();

    // Create the audio processor graph
    graph_ = std::make_unique<juce::AudioProcessorGraph>();
}

VSTChain::~VSTChain()
{
    // Wait for any async loading to finish before destroying
    if (loadThread_ && loadThread_->joinable())
        loadThread_->join();

    releaseResources();
    editorWindows_.clear();
    chain_.clear();
    graph_.reset();
}

void VSTChain::prepareToPlay(double sampleRate, int blockSize)
{
    currentSampleRate_ = sampleRate;
    currentBlockSize_ = blockSize;

    graph_->setPlayConfigDetails(2, 2, sampleRate, blockSize);
    graph_->prepareToPlay(sampleRate, blockSize);

    // Remove old I/O nodes to prevent accumulation on repeated prepareToPlay calls
    if (inputNodeId_.uid != 0)
        graph_->removeNode(inputNodeId_);
    if (outputNodeId_.uid != 0)
        graph_->removeNode(outputNodeId_);

    // Add fresh I/O nodes
    auto inputNode = graph_->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    auto outputNode = graph_->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    if (inputNode && outputNode) {
        inputNodeId_ = inputNode->nodeID;
        outputNodeId_ = outputNode->nodeID;
    }

    // Pre-allocate MidiBuffer to avoid RT allocation
    emptyMidi_.ensureSize(256);
    emptyMidi_.clear();

    rebuildGraph();
    prepared_ = true;
}

void VSTChain::releaseResources()
{
    prepared_ = false;
    graph_->releaseResources();
}

void VSTChain::processBlock(juce::AudioBuffer<float>& buffer, int numSamples)
{
    if (!prepared_) return;

    // Process through the graph — buffer size must match prepareToPlay blockSize
    // If mismatched, skip processing rather than allocating in RT thread
    if (buffer.getNumSamples() != numSamples) return;

    graph_->processBlock(buffer, emptyMidi_);

    // Clear immediately after processing to prevent MIDI output accumulation
    // (avoids heap growth if plugins write MIDI into the buffer)
    emptyMidi_.clear();
}

void VSTChain::scanForPlugins(const juce::StringArray& directoriesToScan)
{
    juce::FileSearchPath searchPath;
    for (const auto& dir : directoriesToScan) {
        searchPath.add(juce::File(dir));
    }

    for (int i = 0; i < formatManager_.getNumFormats(); ++i) {
        auto* format = formatManager_.getFormat(i);
        juce::PluginDirectoryScanner scanner(
            knownPlugins_, *format, searchPath,
            true,   // recursive
            juce::File()  // dead-mans-pedal file
        );

        juce::String pluginName;
        while (scanner.scanNextFile(true, pluginName)) {
            // Scanning in progress
        }
    }
}

int VSTChain::addPlugin(const juce::PluginDescription& desc)
{
    // Load the plugin directly from the description (preferred for scanned plugins)
    juce::String error;
    auto instance = loadPlugin(desc, error);
    if (!instance) {
        juce::Logger::writeToLog("Failed to load plugin: " + error);
        if (onPluginLoadFailed) onPluginLoadFailed(desc.name, error);
        return -1;
    }

    // Add to graph
    auto node = graph_->addNode(std::move(instance));
    if (!node) {
        juce::Logger::writeToLog("Failed to add plugin to graph");
        if (onPluginLoadFailed) onPluginLoadFailed(desc.name, "Failed to add to audio graph");
        return -1;
    }

    // Create plugin slot
    PluginSlot slot;
    slot.name = desc.name;
    slot.path = desc.fileOrIdentifier;
    slot.desc = desc;
    slot.nodeId = node->nodeID;
    slot.instance = dynamic_cast<juce::AudioPluginInstance*>(node->getProcessor());

    int resultIdx;
    {
        const juce::ScopedLock sl(chainLock_);
        chain_.push_back(slot);
        rebuildGraph();
        resultIdx = static_cast<int>(chain_.size()) - 1;
    }

    if (onChainChanged) onChainChanged();

    return resultIdx;
}

int VSTChain::addPlugin(const juce::String& pluginPath)
{
    // Find the plugin description from known plugins or load directly
    juce::PluginDescription desc;
    bool found = false;

    // Search in known plugins first
    for (const auto& d : knownPlugins_.getTypes()) {
        if (d.fileOrIdentifier == pluginPath) {
            desc = d;
            found = true;
            break;
        }
    }

    if (!found) {
        // Try to scan the specific file
        juce::OwnedArray<juce::PluginDescription> descriptions;
        for (int i = 0; i < formatManager_.getNumFormats(); ++i) {
            formatManager_.getFormat(i)->findAllTypesForFile(descriptions, pluginPath);
        }

        if (descriptions.isEmpty()) {
            juce::Logger::writeToLog("Plugin not found: " + pluginPath);
            if (onPluginLoadFailed) onPluginLoadFailed(pluginPath, "Plugin file not found");
            return -1;
        }
        desc = *descriptions[0];
    }

    // Load the plugin
    juce::String error;
    auto instance = loadPlugin(desc, error);
    if (!instance) {
        juce::Logger::writeToLog("Failed to load plugin: " + error);
        if (onPluginLoadFailed) onPluginLoadFailed(desc.name, error);
        return -1;
    }

    // Add to graph
    auto node = graph_->addNode(std::move(instance));
    if (!node) {
        juce::Logger::writeToLog("Failed to add plugin to graph");
        if (onPluginLoadFailed) onPluginLoadFailed(desc.name, "Failed to add to audio graph");
        return -1;
    }

    // Create plugin slot
    PluginSlot slot;
    slot.name = desc.name;
    slot.path = pluginPath;
    slot.desc = desc;
    slot.nodeId = node->nodeID;
    slot.instance = dynamic_cast<juce::AudioPluginInstance*>(node->getProcessor());

    int resultIdx;
    {
        const juce::ScopedLock sl(chainLock_);
        chain_.push_back(slot);
        rebuildGraph();
        resultIdx = static_cast<int>(chain_.size()) - 1;
    }

    if (onChainChanged) onChainChanged();

    return resultIdx;
}

bool VSTChain::removePlugin(int index)
{
    {
        const juce::ScopedLock sl(chainLock_);

        if (index < 0 || index >= static_cast<int>(chain_.size()))
            return false;

        // Close editor window for this plugin
        closePluginEditor(index);

        // Shift editor windows down (remove slot at index)
        if (static_cast<size_t>(index) < editorWindows_.size()) {
            editorWindows_.erase(editorWindows_.begin() + index);
        }

        auto& slot = chain_[static_cast<size_t>(index)];
        graph_->removeNode(slot.nodeId);
        chain_.erase(chain_.begin() + index);
        rebuildGraph();
    }

    // Notify outside of lock to avoid deadlock if listener acquires locks
    if (onChainChanged) onChainChanged();
    return true;
}

bool VSTChain::movePlugin(int fromIndex, int toIndex)
{
    {
        const juce::ScopedLock sl(chainLock_);

        if (fromIndex < 0 || fromIndex >= static_cast<int>(chain_.size()) ||
            toIndex < 0 || toIndex >= static_cast<int>(chain_.size()) ||
            fromIndex == toIndex)
            return false;

        auto slot = chain_[static_cast<size_t>(fromIndex)];
        chain_.erase(chain_.begin() + fromIndex);
        chain_.insert(chain_.begin() + toIndex, slot);

        // Move editor windows to match
        size_t maxIdx = juce::jmax(static_cast<size_t>(fromIndex), static_cast<size_t>(toIndex));
        if (maxIdx < editorWindows_.size()) {
            auto win = std::move(editorWindows_[static_cast<size_t>(fromIndex)]);
            editorWindows_.erase(editorWindows_.begin() + fromIndex);
            editorWindows_.insert(editorWindows_.begin() + toIndex, std::move(win));
        }

        rebuildGraph();
    }

    if (onChainChanged) onChainChanged();
    return true;
}

void VSTChain::setPluginBypassed(int index, bool bypassed)
{
    if (index < 0 || index >= static_cast<int>(chain_.size()))
        return;

    // Skip if no change (avoids unnecessary saves and callbacks)
    if (chain_[static_cast<size_t>(index)].bypassed == bypassed)
        return;

    chain_[static_cast<size_t>(index)].bypassed = bypassed;

    if (auto* node = graph_->getNodeForId(chain_[static_cast<size_t>(index)].nodeId)) {
        node->setBypassed(bypassed);
    }

    if (onChainChanged) onChainChanged();
}

int VSTChain::getPluginCount() const
{
    return static_cast<int>(chain_.size());
}

const PluginSlot* VSTChain::getPluginSlot(int index) const
{
    if (index < 0 || index >= static_cast<int>(chain_.size()))
        return nullptr;
    return &chain_[static_cast<size_t>(index)];
}

int VSTChain::getPluginParameterCount(int pluginIndex) const
{
    auto* slot = getPluginSlot(pluginIndex);
    if (!slot || !slot->instance) return 0;
    return slot->instance->getParameters().size();
}

juce::String VSTChain::getPluginParameterName(int pluginIndex, int paramIndex) const
{
    auto* slot = getPluginSlot(pluginIndex);
    if (!slot || !slot->instance) return {};
    auto& params = slot->instance->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return {};
    return params[paramIndex]->getName(64);
}

void VSTChain::setPluginParameter(int pluginIndex, int paramIndex, float value)
{
    const juce::ScopedLock sl(chainLock_);
    if (pluginIndex < 0 || pluginIndex >= static_cast<int>(chain_.size()))
        return;
    auto* inst = chain_[static_cast<size_t>(pluginIndex)].instance;
    if (!inst) return;
    auto& params = inst->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return;
    params[paramIndex]->setValue(value);
}

float VSTChain::getPluginParameter(int pluginIndex, int paramIndex) const
{
    auto* slot = getPluginSlot(pluginIndex);
    if (!slot || !slot->instance) return 0.0f;
    auto& params = slot->instance->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return 0.0f;
    return params[paramIndex]->getValue();
}

void VSTChain::openPluginEditor(int index, juce::Component* /*parentComponent*/)
{
    if (index < 0 || index >= static_cast<int>(chain_.size()))
        return;

    // Ensure the editor windows vector is large enough
    if (static_cast<size_t>(index) >= editorWindows_.size())
        editorWindows_.resize(static_cast<size_t>(index) + 1);

    // If window already exists, just show it
    if (editorWindows_[static_cast<size_t>(index)]) {
        editorWindows_[static_cast<size_t>(index)]->setVisible(true);
        editorWindows_[static_cast<size_t>(index)]->toFront(true);
        return;
    }

    auto& slot = chain_[static_cast<size_t>(index)];
    if (!slot.instance) return;

    auto* editor = slot.instance->createEditorIfNeeded();
    if (!editor) return;

    auto window = std::make_unique<PluginEditorWindow>(slot.name);
    window->setContentOwned(editor, true);
    window->setResizable(false, false);
    window->centreWithSize(editor->getWidth(), editor->getHeight());
    window->setVisible(true);
    window->setAlwaysOnTop(true);
    window->onClosed = [this] {
        if (onEditorClosed) onEditorClosed();
    };

    editorWindows_[static_cast<size_t>(index)] = std::move(window);
}

void VSTChain::closePluginEditor(int index)
{
    if (index >= 0 && static_cast<size_t>(index) < editorWindows_.size()) {
        editorWindows_[static_cast<size_t>(index)].reset();
    }
}

void VSTChain::rebuildGraph()
{
    // Suspend graph processing while rebuilding connections
    // This prevents the RT thread from calling processBlock on a half-built graph
    graph_->suspendProcessing(true);

    // Remove all existing connections only (preserve nodes)
    for (auto conn : graph_->getConnections()) {
        graph_->removeConnection(conn);
    }

    if (chain_.empty()) {
        // Direct connection: input -> output
        for (int ch = 0; ch < 2; ++ch) {
            graph_->addConnection({
                {inputNodeId_, ch},
                {outputNodeId_, ch}
            });
        }
        graph_->suspendProcessing(false);
        return;
    }

    // Build serial chain: Input -> Plugin[0] -> ... -> Plugin[N-1] -> Output
    auto prevNodeId = inputNodeId_;
    for (size_t i = 0; i < chain_.size(); ++i) {
        auto& slot = chain_[i];
        for (int ch = 0; ch < 2; ++ch) {
            graph_->addConnection({
                {prevNodeId, ch},
                {slot.nodeId, ch}
            });
        }
        prevNodeId = slot.nodeId;
    }

    // Last plugin -> Output
    for (int ch = 0; ch < 2; ++ch) {
        graph_->addConnection({
            {prevNodeId, ch},
            {outputNodeId_, ch}
        });
    }

    // Resume processing now that the graph is fully wired
    graph_->suspendProcessing(false);
}

std::unique_ptr<juce::AudioPluginInstance> VSTChain::loadPlugin(
    const juce::PluginDescription& desc, juce::String& error)
{
    return formatManager_.createPluginInstance(
        desc, currentSampleRate_, currentBlockSize_, error);
}

void VSTChain::replaceChainAsync(std::vector<PluginLoadRequest> requests,
                                  std::function<void()> onComplete)
{
    // Wait for any previous async load to finish
    if (loadThread_ && loadThread_->joinable())
        loadThread_->join();

    // Clear current chain immediately (audio goes silent)
    {
        const juce::ScopedLock sl(chainLock_);
        editorWindows_.clear();
        for (auto& slot : chain_)
            graph_->removeNode(slot.nodeId);
        chain_.clear();
        rebuildGraph();
    }

    asyncLoading_.store(true);

    // Capture values needed by background thread
    double sr = currentSampleRate_;
    int bs = currentBlockSize_;

    // Use a shared struct to pass loaded plugins from background thread to message thread
    struct AsyncLoadResult {
        struct Entry {
            std::unique_ptr<juce::AudioPluginInstance> instance;
            PluginLoadRequest request;
        };
        std::vector<Entry> entries;
        std::vector<std::pair<juce::String, juce::String>> failures;
    };
    auto result = std::make_shared<AsyncLoadResult>();

    loadThread_ = std::make_unique<std::thread>(
        [this, requests = std::move(requests), onComplete = std::move(onComplete),
         sr, bs, result]()
    {
        for (auto& req : requests) {
            juce::String error;
            auto inst = formatManager_.createPluginInstance(req.desc, sr, bs, error);
            if (inst)
                result->entries.push_back({std::move(inst), std::move(req)});
            else {
                juce::Logger::writeToLog("Async load failed: " + req.name + " - " + error);
                result->failures.push_back({req.name, error});
            }
        }

        // Post to message thread to wire into graph
        juce::MessageManager::callAsync(
            [this, result, onComplete]()
        {
            const juce::ScopedLock sl(chainLock_);

            for (auto& entry : result->entries) {
                auto node = graph_->addNode(std::move(entry.instance));
                if (!node) continue;

                PluginSlot slot;
                slot.name = entry.request.name;
                slot.path = entry.request.path;
                slot.desc = entry.request.desc;
                slot.nodeId = node->nodeID;
                slot.instance = dynamic_cast<juce::AudioPluginInstance*>(node->getProcessor());
                slot.bypassed = entry.request.bypassed;
                chain_.push_back(slot);

                if (slot.bypassed)
                    node->setBypassed(true);

                if (entry.request.hasState && slot.instance)
                    slot.instance->setStateInformation(
                        entry.request.stateData.getData(),
                        static_cast<int>(entry.request.stateData.getSize()));
            }

            rebuildGraph();
            asyncLoading_.store(false);

            // Report any load failures
            if (onPluginLoadFailed) {
                for (auto& [name, err] : result->failures)
                    onPluginLoadFailed(name, err);
            }

            if (onChainChanged) onChainChanged();
            if (onComplete) onComplete();
        });
    });
}

} // namespace directpipe
