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
 * @file StatusUpdater.h
 * @brief Periodic UI status updates: mute indicators, status bar labels,
 *        level meters, input gain sync, and state broadcaster.
 *
 * Called every 30Hz timer tick from MainComponent. Owns all cached values
 * to avoid redundant repaints and string reconstructions.
 */
#pragma once

#include <JuceHeader.h>

namespace directpipe {

class AudioEngine;
class PresetManager;
class StateBroadcaster;
class LevelMeter;

class StatusUpdater {
public:
    StatusUpdater(AudioEngine& engine, StateBroadcaster& broadcaster);

    /** Bind UI component pointers. Must be called before tick(). */
    void setUI(juce::Label* latencyLabel, juce::Label* cpuLabel, juce::Label* formatLabel,
               juce::TextButton* outputMuteBtn, juce::TextButton* monitorMuteBtn,
               juce::TextButton* vstMuteBtn, juce::Slider* inputGainSlider,
               LevelMeter* inputMeter, LevelMeter* outputMeter);

    /** Perform all periodic updates. Called every timer tick (~30Hz). */
    void tick(PresetManager* pm, int numPresetSlots);

private:
    AudioEngine& engine_;
    StateBroadcaster& broadcaster_;

    // UI component pointers (set by setUI, not owned)
    juce::Label* latencyLabel_ = nullptr;
    juce::Label* cpuLabel_ = nullptr;
    juce::Label* formatLabel_ = nullptr;
    juce::TextButton* outputMuteBtn_ = nullptr;
    juce::TextButton* monitorMuteBtn_ = nullptr;
    juce::TextButton* vstMuteBtn_ = nullptr;
    juce::Slider* inputGainSlider_ = nullptr;
    LevelMeter* inputMeter_ = nullptr;
    LevelMeter* outputMeter_ = nullptr;

    // Cached mute states
    bool cachedOutputMuted_ = false;
    bool cachedMonitorMuted_ = false;
    bool cachedVstEnabled_ = false;

    // Cached status bar values
    double cachedMainLatency_ = -1.0;
    double cachedMonitorLatency_ = -1.0;
    bool cachedMonEnabled_ = false;
    double cachedCpuPercent_ = -1.0;
    int cachedXruns_ = -1;
    int cachedSampleRate_ = 0;
    int cachedBufferSize_ = 0;
    int cachedChannelMode_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusUpdater)
};

} // namespace directpipe
