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
#include "PluginLoadHelper.h"
#include "../Control/Log.h"

#if JUCE_WINDOWS
 #include <objbase.h>   // CoInitializeEx / CoUninitialize (VST3 COM requirement)
#endif

namespace directpipe {

namespace {
    juce::String dumpPluginParams(const juce::AudioPluginInstance* inst, int maxParams = 50) {
        if (!inst) return "(null)";
        try {
            auto& params = inst->getParameters();
            juce::String s;
            int count = juce::jmin(params.size(), maxParams);
            for (int p = 0; p < count; ++p) {
                if (p > 0) s += ", ";
                s += params[p]->getName(32) + "=" + juce::String(params[p]->getValue(), 4);
            }
            if (params.size() > maxParams)
                s += " ... (" + juce::String(params.size()) + " total)";
            return s;
        } catch (...) {
            return "(error reading params)";
        }
    }

    juce::String buildChainOrderStr(const std::vector<PluginSlot>& chain) {
        if (chain.empty()) return "(empty)";
        juce::String s;
        for (size_t i = 0; i < chain.size(); ++i) {
            if (i > 0) s += " -> ";
            s += "[" + juce::String(i) + "] " + chain[i].name;
            s += "(bypass=" + juce::String(chain[i].bypassed ? "Y" : "N");
            try {
                if (chain[i].instance)
                    s += ", params=" + juce::String(chain[i].instance->getParameters().size());
            } catch (...) {
                s += ", params=ERR";
            }
            s += ")";
        }
        return s;
    }
} // anonymous namespace

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
    alive_->store(false);

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

    // Suspend processing during graph structural changes (I/O node swap)
    graph_->suspendProcessing(true);

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

    int pluginCount;
    {
        const juce::ScopedLock sl(chainLock_);
        pluginCount = static_cast<int>(chain_.size());
    }
    prepared_ = true;
    juce::Logger::writeToLog("[VST] Prepare: " + juce::String(sampleRate) + "Hz, " + juce::String(blockSize) + " samples, " + juce::String(pluginCount) + " plugins");
}

void VSTChain::releaseResources()
{
    prepared_ = false;
    graph_->releaseResources();
}

void VSTChain::processBlock(juce::AudioBuffer<float>& buffer, int numSamples)
{
    // RT thread only — must NOT be called from the message thread
    jassert(!juce::MessageManager::getInstanceWithoutCreating()
            || !juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (!prepared_.load(std::memory_order_acquire)) return;

    // Safety: clamp numSamples to buffer capacity
    if (numSamples > buffer.getNumSamples()) return;

    // Use a lightweight sub-buffer view instead of mutating the buffer size.
    // This avoids setSize overhead (channel pointer recalculation) on every callback.
    if (numSamples < buffer.getNumSamples()) {
        juce::AudioBuffer<float> subBuffer(buffer.getArrayOfWritePointers(),
                                            buffer.getNumChannels(), numSamples);
        graph_->processBlock(subBuffer, emptyMidi_);
    } else {
        graph_->processBlock(buffer, emptyMidi_);
    }

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
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Prevent adding plugins while async chain replacement is in progress
    if (asyncLoading_.load()) {
        juce::Logger::writeToLog("[VST] Blocked addPlugin during async chain load: " + desc.name);
        return -1;
    }

    // Load the plugin directly from the description (preferred for scanned plugins)
    juce::String error;
    auto instance = loadPlugin(desc, error);
    if (!instance) {
        juce::Logger::writeToLog("[VST] Failed to load: " + desc.name + " - " + error);
        if (onPluginLoadFailed) onPluginLoadFailed(desc.name, error);
        return -1;
    }

    // addNode inserts into the node list — the render sequence is not rebuilt
    // until rebuildGraph() which handles its own suspend/resume pair.
    // Do NOT suspendProcessing here: JUCE uses a counter, so an extra
    // suspend(true) without a matching suspend(false) leaves the graph muted.
    auto node = graph_->addNode(std::move(instance));
    if (!node) {
        juce::Logger::writeToLog("[VST] Failed to add to graph: " + desc.name);
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
    juce::String auditOrder;
    {
        const juce::ScopedLock sl(chainLock_);
        chain_.push_back(slot);
        rebuildGraph();  // rebuildGraph calls suspendProcessing(false) internally
        resultIdx = static_cast<int>(chain_.size()) - 1;
        if (Log::isAuditMode())
            auditOrder = buildChainOrderStr(chain_);
    }

    juce::Logger::writeToLog("[VST] Loaded: \"" + desc.name + "\" (" + desc.pluginFormatName + ") at index " + juce::String(resultIdx) + " - " + juce::String(desc.numInputChannels) + "in/" + juce::String(desc.numOutputChannels) + "out");
    if (auditOrder.isNotEmpty())
        Log::audit("VST", auditOrder);
    if (onChainChanged) onChainChanged();

    return resultIdx;
}

int VSTChain::addPlugin(const juce::String& pluginPath)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Prevent adding plugins while async chain replacement is in progress
    if (asyncLoading_.load()) {
        juce::Logger::writeToLog("[VST] Blocked addPlugin during async chain load: " + pluginPath);
        return -1;
    }

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
            juce::Logger::writeToLog("[VST] Plugin not found: " + pluginPath);
            if (onPluginLoadFailed) onPluginLoadFailed(pluginPath, "Plugin file not found");
            return -1;
        }
        desc = *descriptions[0];
    }

    // Load the plugin
    juce::String error;
    auto instance = loadPlugin(desc, error);
    if (!instance) {
        juce::Logger::writeToLog("[VST] Failed to load: " + desc.name + " - " + error);
        if (onPluginLoadFailed) onPluginLoadFailed(desc.name, error);
        return -1;
    }

    // See addPlugin(PluginDescription) comment — no suspendProcessing here
    auto node = graph_->addNode(std::move(instance));
    if (!node) {
        juce::Logger::writeToLog("[VST] Failed to add to graph: " + desc.name);
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
    juce::String auditOrder;
    {
        const juce::ScopedLock sl(chainLock_);
        chain_.push_back(slot);
        rebuildGraph();
        resultIdx = static_cast<int>(chain_.size()) - 1;
        if (Log::isAuditMode())
            auditOrder = buildChainOrderStr(chain_);
    }

    juce::Logger::writeToLog("[VST] Loaded: \"" + desc.name + "\" (" + desc.pluginFormatName + ") at index " + juce::String(resultIdx) + " - " + juce::String(desc.numInputChannels) + "in/" + juce::String(desc.numOutputChannels) + "out");
    if (auditOrder.isNotEmpty())
        Log::audit("VST", auditOrder);
    if (onChainChanged) onChainChanged();

    return resultIdx;
}

bool VSTChain::removePlugin(int index)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    // Close editor window BEFORE acquiring chainLock_ to avoid
    // GUI destruction under lock (plugin editors may trigger JUCE callbacks)
    closePluginEditor(index);

    juce::String logMsg;
    juce::String auditOrder;
    int newCount = 0;
    {
        const juce::ScopedLock sl(chainLock_);

        if (index < 0 || index >= static_cast<int>(chain_.size()))
            return false;

        // Erase editor window slot (already reset by closePluginEditor above)
        if (static_cast<size_t>(index) < editorWindows_.size()) {
            editorWindows_.erase(editorWindows_.begin() + index);
        }

        juce::String removedName = chain_[static_cast<size_t>(index)].name;
        int oldCount = static_cast<int>(chain_.size());
        auto& slot = chain_[static_cast<size_t>(index)];
        graph_->removeNode(slot.nodeId);
        chain_.erase(chain_.begin() + index);
        rebuildGraph();
        newCount = static_cast<int>(chain_.size());
        logMsg = "[VST] Removed: \"" + removedName + "\" at index " + juce::String(index) + " (" + juce::String(oldCount) + " -> " + juce::String(newCount) + " plugins)";
        if (Log::isAuditMode())
            auditOrder = buildChainOrderStr(chain_);
    }

    juce::Logger::writeToLog(logMsg);
    if (auditOrder.isNotEmpty())
        Log::audit("VST", auditOrder);
    // Notify outside of lock to avoid deadlock if listener acquires locks
    if (onChainChanged) onChainChanged();
    return true;
}

bool VSTChain::movePlugin(PluginIndex from, PluginIndex to)
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    int fromIndex = from.value;
    int toIndex = to.value;

    juce::String logMsg;
    juce::String auditOrder;
    {
        const juce::ScopedLock sl(chainLock_);

        if (fromIndex < 0 || fromIndex >= static_cast<int>(chain_.size()) ||
            toIndex < 0 || toIndex >= static_cast<int>(chain_.size()) ||
            fromIndex == toIndex)
            return false;

        auto slot = chain_[static_cast<size_t>(fromIndex)];
        juce::String movedName = slot.name;
        chain_.erase(chain_.begin() + fromIndex);
        chain_.insert(chain_.begin() + toIndex, slot);

        // Ensure editor windows vector is large enough, then move to match
        if (editorWindows_.size() < chain_.size())
            editorWindows_.resize(chain_.size());
        if (static_cast<size_t>(fromIndex) < editorWindows_.size()) {
            auto win = std::move(editorWindows_[static_cast<size_t>(fromIndex)]);
            editorWindows_.erase(editorWindows_.begin() + fromIndex);
            editorWindows_.insert(editorWindows_.begin() + toIndex, std::move(win));
        }

        rebuildGraph();
        logMsg = "[VST] Moved: \"" + movedName + "\" from index " + juce::String(fromIndex) + " to " + juce::String(toIndex);
        if (Log::isAuditMode())
            auditOrder = buildChainOrderStr(chain_);
    }

    juce::Logger::writeToLog(logMsg);
    if (auditOrder.isNotEmpty())
        Log::audit("VST", auditOrder);
    if (onChainChanged) onChainChanged();
    return true;
}

void VSTChain::togglePluginBypassed(int index)
{
    bool current;
    {
        const juce::ScopedLock sl(chainLock_);
        if (index < 0 || index >= static_cast<int>(chain_.size()))
            return;
        current = chain_[static_cast<size_t>(index)].bypassed;
    }
    setPluginBypassed(index, !current);
}

bool VSTChain::isPluginBypassed(int index) const
{
    const juce::ScopedLock sl(chainLock_);
    if (index < 0 || index >= static_cast<int>(chain_.size()))
        return false;
    return chain_[static_cast<size_t>(index)].bypassed;
}

void VSTChain::setPluginBypassed(int index, bool bypassed)
{
    juce::String logMsg;
    {
        const juce::ScopedLock sl(chainLock_);

        if (index < 0 || index >= static_cast<int>(chain_.size()))
            return;

        // Skip if no change (avoids unnecessary saves and callbacks)
        if (chain_[static_cast<size_t>(index)].bypassed == bypassed)
            return;

        chain_[static_cast<size_t>(index)].bypassed = bypassed;
        logMsg = "[VST] Bypass: \"" + chain_[static_cast<size_t>(index)].name + "\" [" + juce::String(index) + "] = " + (bypassed ? "true" : "false");

        // Sync node + plugin bypass state. replaceChainWithPreloaded sets
        // node->setBypassed(true) + bypass parameter = 1.0 for cached plugins.
        // Without clearing these here, un-bypassing leaves the plugin's internal
        // bypass active (e.g., Clear, RNNoise with getBypassParameter()).
        if (auto* node = graph_->getNodeForId(chain_[static_cast<size_t>(index)].nodeId)) {
            node->setBypassed(bypassed);
            if (auto* bp = node->getProcessor()->getBypassParameter())
                bp->setValueNotifyingHost(bypassed ? 1.0f : 0.0f);
        }

        // Rebuild graph connections: bypassed plugins are disconnected from the
        // signal chain (audio routes around them). This is more reliable than
        // JUCE's node->setBypassed() which doesn't work for plugins that have
        // their own bypass parameter (VST2 canDo("bypass"), VST3 bypass param).
        // suspend=false: only connections change (no nodes added/removed), so
        // the render sequence swaps atomically without audio gap.
        rebuildGraph(/*suspend=*/false);
    }

    juce::Logger::writeToLog(logMsg);
    // Notify outside of lock scope (deadlock prevention, consistent with removePlugin/movePlugin)
    if (onChainChanged) onChainChanged();
}

int VSTChain::getPluginCount() const
{
    const juce::ScopedLock sl(chainLock_);
    return static_cast<int>(chain_.size());
}

std::vector<PluginLatencyInfo> VSTChain::getPluginLatencies() const
{
    const juce::ScopedLock sl(chainLock_);
    std::vector<PluginLatencyInfo> result;
    result.reserve(chain_.size());

    // Get sample rate from graph if available
    double sr = graph_ ? graph_->getSampleRate() : 48000.0;

    for (const auto& slot : chain_) {
        PluginLatencyInfo info;
        if (slot.instance != nullptr) {
            info.latencySamples = slot.instance->getLatencySamples();
            if (sr > 0.0)
                info.latencyMs = static_cast<float>(info.latencySamples) / static_cast<float>(sr) * 1000.0f;
        }
        result.push_back(info);
    }
    return result;
}

int VSTChain::getTotalChainPDC() const
{
    const juce::ScopedLock sl(chainLock_);
    if (graph_ != nullptr)
        return graph_->getLatencySamples();
    return 0;
}

const PluginSlot* VSTChain::getPluginSlot(int index) const
{
    const juce::ScopedLock sl(chainLock_);
    if (index < 0 || index >= static_cast<int>(chain_.size()))
        return nullptr;
    return &chain_[static_cast<size_t>(index)];
}

int VSTChain::getPluginParameterCount(int pluginIndex) const
{
    const juce::ScopedLock sl(chainLock_);
    if (pluginIndex < 0 || pluginIndex >= static_cast<int>(chain_.size()))
        return 0;
    auto* inst = chain_[static_cast<size_t>(pluginIndex)].instance;
    if (!inst) return 0;
    return inst->getParameters().size();
}

juce::String VSTChain::getPluginParameterName(int pluginIndex, int paramIndex) const
{
    const juce::ScopedLock sl(chainLock_);
    if (pluginIndex < 0 || pluginIndex >= static_cast<int>(chain_.size()))
        return {};
    auto* inst = chain_[static_cast<size_t>(pluginIndex)].instance;
    if (!inst) return {};
    auto& params = inst->getParameters();
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
    const juce::ScopedLock sl(chainLock_);
    if (pluginIndex < 0 || pluginIndex >= static_cast<int>(chain_.size()))
        return 0.0f;
    auto* inst = chain_[static_cast<size_t>(pluginIndex)].instance;
    if (!inst) return 0.0f;
    auto& params = inst->getParameters();
    if (paramIndex < 0 || paramIndex >= params.size()) return 0.0f;
    return params[paramIndex]->getValue();
}

void VSTChain::openPluginEditor(int index, juce::Component* /*parentComponent*/)
{
    juce::AudioPluginInstance* pluginInstance = nullptr;
    juce::String pluginName;

    // First lock scope: validate index, check existing window, extract plugin data
    {
        const juce::ScopedLock sl(chainLock_);
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

        pluginInstance = slot.instance;
        pluginName = slot.name;
    }
    // Lock released — safe to create GUI without risk of deadlock from plugin callbacks

    auto* editor = pluginInstance->createEditorIfNeeded();
    if (!editor) return;

    auto window = std::make_unique<PluginEditorWindow>(pluginName);
    window->setContentOwned(editor, true);
    window->setResizable(false, false);
    window->centreWithSize(editor->getWidth(), editor->getHeight());
    window->setVisible(true);
    window->setAlwaysOnTop(true);
    juce::Logger::writeToLog("[VST] Editor opened: \"" + pluginName + "\" [" + juce::String(index) + "]");
    if (Log::isAuditMode())
        Log::audit("VST", "Editor params [" + juce::String(index) + "] \"" + pluginName + "\": " + dumpPluginParams(pluginInstance));
    auto aliveFlag = alive_;
    window->onClosed = [this, aliveFlag] {
        if (!aliveFlag->load()) return;
        if (onEditorClosed) onEditorClosed();
    };

    // Second lock scope: store the window in editorWindows_
    {
        const juce::ScopedLock sl(chainLock_);
        // Re-validate index (chain may have been modified while lock was released)
        if (index >= 0 && index < static_cast<int>(chain_.size())) {
            if (static_cast<size_t>(index) >= editorWindows_.size())
                editorWindows_.resize(static_cast<size_t>(index) + 1);
            editorWindows_[static_cast<size_t>(index)] = std::move(window);
        }
    }
}

void VSTChain::closePluginEditor(int index)
{
    juce::String auditMsg;
    {
        const juce::ScopedLock sl(chainLock_);
        if (index >= 0 && static_cast<size_t>(index) < editorWindows_.size()) {
            if (editorWindows_[static_cast<size_t>(index)]) {
                if (Log::isAuditMode() && index < static_cast<int>(chain_.size()))
                    auditMsg = "Editor closed [" + juce::String(index) + "] \"" + chain_[static_cast<size_t>(index)].name + "\"";
                editorWindows_[static_cast<size_t>(index)].reset();
            }
        }
    }
    if (auditMsg.isNotEmpty())
        Log::audit("VST", auditMsg);
}

// ─── rebuildGraph: 그래프 연결 재구성 ─────────────────────────
// suspend=true: 노드 추가/제거 시 (오디오 갭 발생 가능)
// suspend=false: 바이패스 토글 시 (연결만 변경, 갭 없음)
// 바이패스된 플러그인은 연결 그래프에서 건너뜀
// WARNING: getConnections() 복사 후 제거 루프 실행 (이터레이터 안전)
// ──────────────────────────────────────────────────────────────
void VSTChain::rebuildGraph(bool suspend)
{
    using UK = juce::AudioProcessorGraph::UpdateKind;

    // When nodes are added/removed, suspend processing to prevent the RT thread
    // from accessing a half-built graph. When only connections change (bypass
    // toggle), suspension is unnecessary — the render sequence swaps atomically
    // via UK::sync, so the audio thread sees either the old or new sequence.
    if (suspend)
        graph_->suspendProcessing(true);

    // Guard: I/O nodes must exist (created in prepareToPlay)
    if (inputNodeId_.uid == 0 || outputNodeId_.uid == 0) {
        if (suspend) graph_->suspendProcessing(false);
        return;
    }

    // Remove all existing connections (async — defer render sequence rebuild).
    // Copy first: removeConnection may modify the internal array, invalidating
    // the range-based for iterator. Explicit copy avoids undefined behavior.
    {
        auto conns = graph_->getConnections();
        for (const auto& conn : conns)
            graph_->removeConnection(conn, UK::async);
    }

    if (chain_.empty()) {
        // Direct connection: input -> output (last connection triggers rebuild)
        graph_->addConnection({{inputNodeId_, 0}, {outputNodeId_, 0}}, UK::async);
        graph_->addConnection({{inputNodeId_, 1}, {outputNodeId_, 1}}, UK::sync);
        if (suspend) graph_->suspendProcessing(false);
        return;
    }

    // Build serial chain: Input -> Plugin[0] -> ... -> Plugin[N-1] -> Output
    // Bypassed plugins are skipped in the connection graph (audio routes around them).
    // This is more reliable than JUCE's node->setBypassed() which doesn't work for
    // VST2/VST3 plugins that report their own bypass parameter (getBypassParameter()).
    // All connections use async except the very last one (triggers single rebuild)
    auto prevNodeId = inputNodeId_;
    for (size_t i = 0; i < chain_.size(); ++i) {
        auto& slot = chain_[i];
        if (slot.bypassed) continue;  // skip bypassed plugins in connection graph
        for (int ch = 0; ch < 2; ++ch) {
            graph_->addConnection({
                {prevNodeId, ch},
                {slot.nodeId, ch}
            }, UK::async);
        }
        prevNodeId = slot.nodeId;
    }

    // Last non-bypassed plugin (or input) -> Output (final connection triggers the single rebuild)
    graph_->addConnection({{prevNodeId, 0}, {outputNodeId_, 0}}, UK::async);
    graph_->addConnection({{prevNodeId, 1}, {outputNodeId_, 1}}, UK::sync);

    if (suspend)
        graph_->suspendProcessing(false);
}

std::unique_ptr<juce::AudioPluginInstance> VSTChain::loadPlugin(
    const juce::PluginDescription& desc, juce::String& error)
{
    try {
        return formatManager_.createPluginInstance(
            desc, currentSampleRate_, currentBlockSize_, error);
    }
    catch (const std::exception& e) {
        error = "Plugin threw exception: " + juce::String(e.what());
        return nullptr;
    }
    catch (...) {
        error = "Plugin crashed during initialization (unknown exception)";
        return nullptr;
    }
}

// ─── replaceChainAsync: Background Plugin Loading ───────────────────────
// Pattern: Keep-Old-Until-Ready
//   1. 현재 체인은 계속 오디오 처리 (끊김 없음)
//   2. BG 스레드에서 새 플러그인 로드 (DLL 로딩은 느림)
//   3. callAsync로 Message 스레드에서 그래프 교체 (alive_ 가드)
//   4. asyncGeneration_ 카운터로 오래된 로드 폐기 (새 요청이 이전 요청을 대체)
// WARNING: Windows에서 COM STA 초기화 필수 (VST3 플러그인 팩토리)
// WARNING: formatMgr 참조 캡처 — 호출자의 수명이 보장되어야 함
// ────────────────────────────────────────────────────────────────────────
void VSTChain::replaceChainAsync(std::vector<PluginLoadRequest> requests,
                                  std::function<void()> onComplete,
                                  std::function<void()> preWork)
{
    // Wait for any previous async load to finish
    if (loadThread_ && loadThread_->joinable())
        loadThread_->join();

    // Keep-Old-Until-Ready: old chain stays in graph and continues
    // processing audio while new plugins are loaded on background thread.
    // Swap happens atomically on the message thread when loading completes.

    asyncLoading_.store(true);
    juce::Logger::writeToLog("[VST] Async chain load started: " + juce::String(requests.size()) + " plugins");
    uint32_t generation = asyncGeneration_.fetch_add(1) + 1;

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

    auto aliveFlag = alive_;

    loadThread_ = std::make_unique<std::thread>(
        [this, requests = std::move(requests), onComplete = std::move(onComplete),
         preWork = std::move(preWork), sr, bs, result, aliveFlag, generation]()
    {
    #if JUCE_WINDOWS
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        // RAII guard ensures CoUninitialize runs even if plugin loading throws
        struct ComScope { ~ComScope() { CoUninitialize(); } } comGuard;
    #endif

        // Run pre-work (e.g. join preload thread) on background thread
        // to avoid blocking the message thread
        if (preWork) preWork();

        for (auto& req : requests) {
            juce::String error;
            auto inst = createPluginOnCorrectThread(formatManager_, req.desc, sr, bs, error, aliveFlag);
            if (inst)
                result->entries.push_back({std::move(inst), std::move(req)});
            else {
                juce::Logger::writeToLog("ERR [VST] Async load failed: " + req.name + " (path=" + req.path + "): " + error);
                result->failures.push_back({req.name, error});
            }
        }

        // Post to message thread to wire into graph
        juce::MessageManager::callAsync(
            [this, result, onComplete, aliveFlag, generation]()
        {
            if (!aliveFlag->load()) return;
            // Stale callAsync from a superseded replaceChainAsync — discard
            if (asyncGeneration_.load() != generation) return;

            juce::String logMsg;
            juce::String auditChainOrder;
            juce::StringArray auditParams;
            {
                const juce::ScopedLock sl(chainLock_);

                graph_->suspendProcessing(true);

                // Remove OLD chain nodes (async to defer rebuild until the end)
                editorWindows_.clear();
                for (auto& slot : chain_)
                    graph_->removeNode(slot.nodeId,
                        juce::AudioProcessorGraph::UpdateKind::async);
                chain_.clear();

                // Add NEW nodes (async — single rebuild at end)
                for (auto& entry : result->entries) {
                    auto node = graph_->addNode(std::move(entry.instance), {},
                        juce::AudioProcessorGraph::UpdateKind::async);
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

                rebuildGraph();  // single rebuild with connections + suspendProcessing(false)
                logMsg = "[VST] Async chain load complete: " + juce::String(chain_.size()) + " plugins loaded";
                if (Log::isAuditMode()) {
                    auditChainOrder = buildChainOrderStr(chain_);
                    for (size_t i = 0; i < chain_.size(); ++i)
                        auditParams.add("[" + juce::String(i) + "] " + chain_[i].name + ": " + dumpPluginParams(chain_[i].instance));
                }
            }

            juce::Logger::writeToLog(logMsg);
            if (auditChainOrder.isNotEmpty()) {
                Log::audit("VST", auditChainOrder);
                for (auto& p : auditParams)
                    Log::audit("VST", "  " + p);
            }
            asyncLoading_.store(false);

            // Report any load failures (outside lock)
            if (onPluginLoadFailed) {
                for (auto& [name, err] : result->failures)
                    onPluginLoadFailed(name, err);
            }

            if (onChainChanged) onChainChanged();
            if (onComplete) onComplete();
        });
    });
}

void VSTChain::replaceChainWithPreloaded(std::vector<PreloadedPlugin> preloaded,
                                          std::function<void()> onComplete)
{
    auto startMs = juce::Time::getMillisecondCounter();

    // Wait for any previous async load to finish
    if (loadThread_ && loadThread_->joinable())
        loadThread_->join();

    // Invalidate any stale callAsync from previous replaceChainAsync
    asyncGeneration_.fetch_add(1);

    juce::String logMsg;
    juce::String auditChainOrder;
    juce::StringArray auditParams;
    {
        const juce::ScopedLock sl(chainLock_);

        graph_->suspendProcessing(true);

        // Remove OLD chain nodes (async to defer rebuild until the end)
        editorWindows_.clear();
        for (auto& slot : chain_)
            graph_->removeNode(slot.nodeId,
                juce::AudioProcessorGraph::UpdateKind::async);
        chain_.clear();

        // Add pre-loaded nodes (async — single rebuild at end)
        for (auto& entry : preloaded) {
            auto node = graph_->addNode(std::move(entry.instance), {},
                juce::AudioProcessorGraph::UpdateKind::async);
            if (!node) {
                juce::Logger::writeToLog("ERR [VST] Failed to add cached node to graph: " + entry.request.name);
                continue;
            }

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

        rebuildGraph();  // single rebuild with connections + suspendProcessing(false)
        auto elapsed = juce::Time::getMillisecondCounter() - startMs;
        logMsg = "INF [VST] Cached chain swap: " + juce::String(chain_.size())
            + " plugins (" + juce::String(elapsed) + "ms)";
        if (Log::isAuditMode()) {
            auditChainOrder = buildChainOrderStr(chain_);
            for (size_t i = 0; i < chain_.size(); ++i)
                auditParams.add("[" + juce::String(i) + "] " + chain_[i].name + ": " + dumpPluginParams(chain_[i].instance));
        }
    }

    juce::Logger::writeToLog(logMsg);
    if (auditChainOrder.isNotEmpty()) {
        Log::audit("VST", auditChainOrder);
        for (auto& p : auditParams)
            Log::audit("VST", "  " + p);
    }
    asyncLoading_.store(false);

    if (onChainChanged) onChainChanged();
    if (onComplete) onComplete();
}

} // namespace directpipe
