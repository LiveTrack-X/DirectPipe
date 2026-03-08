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
 * @file PluginLoadHelper.h
 * @brief Cross-platform safe plugin instance creation
 *
 * On macOS, some AU/VST3 plugins require AppKit main thread for creation.
 * This helper dispatches plugin creation to the message thread on macOS
 * while keeping it on the calling thread for Windows/Linux.
 */
#pragma once

#include <JuceHeader.h>
#include <atomic>
#include <memory>

namespace directpipe {

/**
 * Creates a plugin instance on the correct thread for the current platform.
 *
 * On macOS, AU/VST3 plugins may require the main thread (AppKit) for creation.
 * When called from a background thread on macOS, this dispatches to the message
 * thread and waits for the result. On Windows/Linux, creates directly on the
 * calling thread (Windows uses COM STA separately).
 *
 * @param formatMgr    Plugin format manager
 * @param desc         Plugin description
 * @param sampleRate   Sample rate for plugin initialization
 * @param blockSize    Block size for plugin initialization
 * @param error        [out] Error message on failure
 * @param alive        Optional alive flag — if false, creation is skipped (shutdown guard)
 * @param cancelToken  Optional cancellation token — checked periodically during wait
 * @return Created plugin instance, or nullptr on failure/cancel/timeout
 */
inline std::unique_ptr<juce::AudioPluginInstance> createPluginOnCorrectThread(
    juce::AudioPluginFormatManager& formatMgr,
    const juce::PluginDescription& desc,
    double sampleRate,
    int blockSize,
    juce::String& error,
    std::shared_ptr<std::atomic<bool>> alive = nullptr,
    std::atomic<bool>* cancelToken = nullptr)
{
#if JUCE_MAC
    // Already on message thread — create directly
    if (juce::MessageManager::getInstance()->isThisTheMessageManagerThread())
        return formatMgr.createPluginInstance(desc, sampleRate, blockSize, error);

    // Shared state for cross-thread communication.
    // shared_ptr ensures no use-after-free if caller times out and stack unwinds.
    struct State {
        std::unique_ptr<juce::AudioPluginInstance> result;
        juce::String error;
        juce::WaitableEvent done;
        std::atomic<bool> abandoned{false};
    };
    auto state = std::make_shared<State>();

    // Capture formatMgr address + alive flag.
    // The lambda runs on the message thread; if alive is false, formatMgr
    // may be destroyed — skip creation. Both lambda and destructor run on
    // the message thread, so no concurrent access is possible.
    auto* fmPtr = &formatMgr;

    juce::MessageManager::callAsync(
        [state, fmPtr, desc, sampleRate, blockSize, alive]()
    {
        if (state->abandoned.load(std::memory_order_acquire)) return;
        if (alive && !alive->load()) { state->done.signal(); return; }
        state->result = fmPtr->createPluginInstance(desc, sampleRate, blockSize, state->error);
        state->done.signal();
    });

    // Poll with cancel support + hard timeout
    constexpr int kPollMs = 100;
    constexpr int kMaxWaitMs = 30000;  // 30s hard limit
    for (int elapsed = 0; elapsed < kMaxWaitMs; elapsed += kPollMs) {
        if (state->done.wait(kPollMs)) {
            error = state->error;
            return std::move(state->result);
        }
        if (cancelToken && cancelToken->load(std::memory_order_relaxed)) {
            state->abandoned.store(true, std::memory_order_release);
            error = "Plugin creation cancelled";
            return nullptr;
        }
    }

    state->abandoned.store(true, std::memory_order_release);
    error = "Plugin creation timed out (AppKit dispatch)";
    return nullptr;

#else
    // Windows/Linux: create directly on calling thread.
    // Windows COM STA is handled separately by the caller.
    (void)alive;
    (void)cancelToken;
    return formatMgr.createPluginInstance(desc, sampleRate, blockSize, error);
#endif
}

} // namespace directpipe
