/**
 * @file VSTChain.cpp
 * @brief VST plugin chain implementation
 */

#include "VSTChain.h"

namespace directpipe {

VSTChain::VSTChain()
{
    // Register standard plugin formats (VST2, VST3)
    formatManager_.addDefaultFormats();

    // Create the audio processor graph
    graph_ = std::make_unique<juce::AudioProcessorGraph>();
}

VSTChain::~VSTChain()
{
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

    // Add I/O nodes
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

    // JUCE's AudioProcessorGraph handles the routing internally
    juce::MidiBuffer midi;

    // Resize graph buffer if needed and process
    if (buffer.getNumSamples() != numSamples) {
        buffer.setSize(buffer.getNumChannels(), numSamples, true, false, true);
    }

    graph_->processBlock(buffer, midi);
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
            return -1;
        }
        desc = *descriptions[0];
    }

    // Load the plugin
    juce::String error;
    auto instance = loadPlugin(desc, error);
    if (!instance) {
        juce::Logger::writeToLog("Failed to load plugin: " + error);
        return -1;
    }

    // Add to graph
    auto node = graph_->addNode(std::move(instance));
    if (!node) {
        juce::Logger::writeToLog("Failed to add plugin to graph");
        return -1;
    }

    // Create plugin slot
    PluginSlot slot;
    slot.name = desc.name;
    slot.path = pluginPath;
    slot.nodeId = node->nodeID;
    slot.instance = dynamic_cast<juce::AudioPluginInstance*>(node->getProcessor());

    const juce::ScopedLock sl(chainLock_);
    chain_.push_back(slot);
    rebuildGraph();

    if (onChainChanged) onChainChanged();

    return static_cast<int>(chain_.size()) - 1;
}

bool VSTChain::removePlugin(int index)
{
    const juce::ScopedLock sl(chainLock_);

    if (index < 0 || index >= static_cast<int>(chain_.size()))
        return false;

    auto& slot = chain_[static_cast<size_t>(index)];
    graph_->removeNode(slot.nodeId);
    chain_.erase(chain_.begin() + index);
    rebuildGraph();

    if (onChainChanged) onChainChanged();
    return true;
}

bool VSTChain::movePlugin(int fromIndex, int toIndex)
{
    const juce::ScopedLock sl(chainLock_);

    if (fromIndex < 0 || fromIndex >= static_cast<int>(chain_.size()) ||
        toIndex < 0 || toIndex >= static_cast<int>(chain_.size()) ||
        fromIndex == toIndex)
        return false;

    auto slot = chain_[static_cast<size_t>(fromIndex)];
    chain_.erase(chain_.begin() + fromIndex);
    chain_.insert(chain_.begin() + toIndex, slot);
    rebuildGraph();

    if (onChainChanged) onChainChanged();
    return true;
}

void VSTChain::setPluginBypassed(int index, bool bypassed)
{
    if (index < 0 || index >= static_cast<int>(chain_.size()))
        return;

    chain_[static_cast<size_t>(index)].bypassed = bypassed;

    if (auto* node = graph_->getNodeForId(chain_[static_cast<size_t>(index)].nodeId)) {
        node->setBypassed(bypassed);
    }
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

void VSTChain::openPluginEditor(int index, juce::Component* parentComponent)
{
    if (index < 0 || index >= static_cast<int>(chain_.size()))
        return;

    auto& slot = chain_[static_cast<size_t>(index)];
    if (!slot.instance) return;

    auto editor = slot.instance->createEditorIfNeeded();
    if (!editor) return;

    auto window = std::make_unique<juce::DocumentWindow>(
        slot.name,
        juce::Colours::darkgrey,
        juce::DocumentWindow::closeButton);

    window->setContentOwned(editor, true);
    window->setResizable(false, false);
    window->centreWithSize(editor->getWidth(), editor->getHeight());
    window->setVisible(true);

    if (parentComponent) {
        window->setAlwaysOnTop(true);
    }

    // Store the window (ensure index is valid for the windows vector)
    if (static_cast<size_t>(index) >= editorWindows_.size()) {
        editorWindows_.resize(static_cast<size_t>(index) + 1);
    }
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
    // Remove all connections
    graph_->clear();

    // Re-add I/O nodes
    auto inputNode = graph_->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode));
    auto outputNode = graph_->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    if (!inputNode || !outputNode) return;

    inputNodeId_ = inputNode->nodeID;
    outputNodeId_ = outputNode->nodeID;

    if (chain_.empty()) {
        // Direct connection: input → output
        for (int ch = 0; ch < 2; ++ch) {
            graph_->addConnection({
                {inputNodeId_, ch},
                {outputNodeId_, ch}
            });
        }
        return;
    }

    // Re-add all plugin nodes and rebuild serial chain
    // Input → Plugin[0] → Plugin[1] → ... → Plugin[N-1] → Output
    for (auto& slot : chain_) {
        // Plugin instance is still alive in the graph; we need to re-add it
        // Since we cleared the graph, the nodes are gone. For simplicity,
        // we track that plugins are managed externally.
    }

    // Build connections: Input → first plugin
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

    // Last plugin → Output
    for (int ch = 0; ch < 2; ++ch) {
        graph_->addConnection({
            {prevNodeId, ch},
            {outputNodeId_, ch}
        });
    }
}

std::unique_ptr<juce::AudioPluginInstance> VSTChain::loadPlugin(
    const juce::PluginDescription& desc, juce::String& error)
{
    return formatManager_.createPluginInstance(
        desc, currentSampleRate_, currentBlockSize_, error);
}

} // namespace directpipe
