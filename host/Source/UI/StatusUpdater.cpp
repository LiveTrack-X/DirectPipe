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
 * @file StatusUpdater.cpp
 * @brief Periodic UI status update implementation
 */

#include "StatusUpdater.h"
#include "../Audio/AudioEngine.h"
#include "../Control/StateBroadcaster.h"
#include "../Control/Log.h"
#include "LevelMeter.h"
#include "PresetManager.h"

namespace directpipe {

StatusUpdater::StatusUpdater(AudioEngine& engine, StateBroadcaster& broadcaster)
    : engine_(engine), broadcaster_(broadcaster)
{
}

void StatusUpdater::setUI(juce::Label* latencyLabel, juce::Label* cpuLabel, juce::Label* formatLabel,
                          juce::TextButton* outputMuteBtn, juce::TextButton* monitorMuteBtn,
                          juce::TextButton* vstMuteBtn, juce::Slider* inputGainSlider,
                          LevelMeter* inputMeter, LevelMeter* outputMeter)
{
    latencyLabel_ = latencyLabel;
    cpuLabel_ = cpuLabel;
    formatLabel_ = formatLabel;
    outputMuteBtn_ = outputMuteBtn;
    monitorMuteBtn_ = monitorMuteBtn;
    vstMuteBtn_ = vstMuteBtn;
    inputGainSlider_ = inputGainSlider;
    inputMeter_ = inputMeter;
    outputMeter_ = outputMeter;
}

void StatusUpdater::tick(PresetManager* pm, int numPresetSlots)
{
    if (!inputMeter_) return;

    if (engine_.getOutputRouter().checkAndClearBufferTruncated())
        Log::warn("AUDIO", "Buffer truncation detected — consider increasing buffer size");

    auto& monitor = engine_.getLatencyMonitor();
    bool muted = engine_.isMuted();

    // ── Level meters ──
    inputMeter_->setLevel(engine_.getInputLevel());
    outputMeter_->setLevel(muted ? 0.0f : engine_.getOutputLevel());
    inputMeter_->tick();
    outputMeter_->tick();

    // ── Mute indicator colours (cached to avoid redundant repaints) ──
    {
        bool outMuted = engine_.isOutputMuted() || muted;
        if (outMuted != cachedOutputMuted_) {
            cachedOutputMuted_ = outMuted;
            outputMuteBtn_->setColour(juce::TextButton::buttonColourId,
                outMuted ? juce::Colour(0xFFE05050) : juce::Colour(0xFF4CAF50));
        }

        bool monMuted = !engine_.getOutputRouter().isEnabled(OutputRouter::Output::Monitor) || muted;
        if (monMuted != cachedMonitorMuted_) {
            cachedMonitorMuted_ = monMuted;
            monitorMuteBtn_->setColour(juce::TextButton::buttonColourId,
                monMuted ? juce::Colour(0xFFE05050) : juce::Colour(0xFF4CAF50));
        }

        bool vstActive = engine_.isIpcEnabled();
        if (vstActive != cachedVstEnabled_) {
            cachedVstEnabled_ = vstActive;
            vstMuteBtn_->setColour(juce::TextButton::buttonColourId,
                vstActive ? juce::Colour(0xFF4CAF50) : juce::Colour(0xFFE05050));
        }
    }

    // ── Latency label ──
    double mainLatency = monitor.getTotalLatencyVirtualMicMs();
    auto& monOut = engine_.getMonitorOutput();
    auto& router = engine_.getOutputRouter();
    bool monEnabled = router.isEnabled(OutputRouter::Output::Monitor);

    {
        double monitorLatency = 0.0;
        if (monEnabled) {
            monitorLatency = mainLatency;
            if (monOut.isActive()) {
                double monSR = monOut.getActualSampleRate();
                if (monSR > 0.0)
                    monitorLatency += (static_cast<double>(monOut.getActualBufferSize()) / monSR) * 1000.0;
            }
        }
        if (std::abs(mainLatency - cachedMainLatency_) > 0.05 ||
            std::abs(monitorLatency - cachedMonitorLatency_) > 0.05 ||
            monEnabled != cachedMonEnabled_)
        {
            cachedMainLatency_ = mainLatency;
            cachedMonitorLatency_ = monitorLatency;
            cachedMonEnabled_ = monEnabled;
            juce::String latencyText = "Latency: " + juce::String(mainLatency, 1) + "ms";
            if (monEnabled)
                latencyText += " | Mon: " + juce::String(monitorLatency, 1) + "ms";
            latencyLabel_->setText(latencyText, juce::dontSendNotification);
        }
    }

    // ── CPU/XRun/LIM label ──
    {
        engine_.updateXRunTracking();
        double cpuPct = monitor.getCpuUsagePercent();
        int xruns = engine_.getRecentXRunCount();
        bool limActive = engine_.getSafetyLimiter().isLimiting();
        if (std::abs(cpuPct - cachedCpuPercent_) > 0.1 || xruns != cachedXruns_ ||
            limActive != cachedLimiterActive_) {
            cachedCpuPercent_ = cpuPct;
            cachedXruns_ = xruns;
            cachedLimiterActive_ = limActive;
            juce::String cpuText = "CPU: " + juce::String(cpuPct, 1) + "%";
            if (xruns > 0)
                cpuText += " | XRun: " + juce::String(xruns);
            if (limActive)
                cpuText += " | [LIM]";
            if (xruns > 0 || limActive) {
                cpuLabel_->setColour(juce::Label::textColourId, juce::Colour(0xFFFF6B6B));
            } else {
                cpuLabel_->setColour(juce::Label::textColourId, juce::Colour(0xFF8888AA));
            }
            cpuLabel_->setText(cpuText, juce::dontSendNotification);
        }
    }

    // ── Format label ──
    {
        int sr = static_cast<int>(monitor.getSampleRate());
        int bs = monitor.getBufferSize();
        int cm = engine_.getChannelMode();
        if (sr != cachedSampleRate_ || bs != cachedBufferSize_ || cm != cachedChannelMode_) {
            cachedSampleRate_ = sr;
            cachedBufferSize_ = bs;
            cachedChannelMode_ = cm;
            formatLabel_->setText(
                juce::String(sr) + "Hz / " + juce::String(bs) + " smp / " +
                juce::String(cm == 1 ? "Mono" : "Stereo"),
                juce::dontSendNotification);
        }
    }

    // ── Input gain slider sync ──
    float currentGain = engine_.getInputGain();
    if (std::abs(static_cast<float>(inputGainSlider_->getValue()) - currentGain) > 0.01f) {
        inputGainSlider_->setValue(currentGain, juce::dontSendNotification);
    }

    // ── Broadcast state to WebSocket clients (Stream Deck, etc.) ──
    auto& chain = engine_.getVSTChain();
    broadcaster_.updateState([&](AppState& s) {
        s.inputGain = engine_.getInputGain();
        s.monitorVolume = router.getVolume(OutputRouter::Output::Monitor);
        s.outputVolume = router.getVolume(OutputRouter::Output::Main);
        s.muted = muted;
        s.outputMuted = engine_.isOutputMuted();
        s.inputMuted = muted;
        s.masterBypassed = false;
        s.latencyMs = static_cast<float>(mainLatency);
        if (monEnabled) {
            double monitorLat = mainLatency;
            if (monOut.isActive()) {
                double monSR2 = monOut.getActualSampleRate();
                if (monSR2 > 0.0)
                    monitorLat += (static_cast<double>(monOut.getActualBufferSize()) / monSR2) * 1000.0;
            }
            s.monitorLatencyMs = static_cast<float>(monitorLat);
        } else {
            s.monitorLatencyMs = 0.0f;
        }
        s.inputLevelDb = engine_.getInputLevel();
        s.cpuPercent = static_cast<float>(monitor.getCpuUsagePercent());
        s.sampleRate = monitor.getSampleRate();
        s.bufferSize = monitor.getBufferSize();
        s.channelMode = engine_.getChannelMode();
        s.monitorEnabled = router.isEnabled(OutputRouter::Output::Monitor);
        {
            int slot = pm ? pm->getActiveSlot() : -1;
            s.activeSlot = (slot >= 0 && slot <= 4) ? slot : -1;
            s.autoSlotActive = (slot == 5);  // PresetSlotBar::kAutoSlotIndex == 5
        }
        s.recording = engine_.getRecorder().isRecording();
        s.recordingSeconds = engine_.getRecorder().getRecordedSeconds();
        s.ipcEnabled = engine_.isIpcEnabled();
        s.xrunCount = engine_.getRecentXRunCount();

        auto& limiter = engine_.getSafetyLimiter();
        s.limiterEnabled = limiter.isEnabled();
        s.limiterCeilingdB = limiter.getCeilingdB();
        s.limiterGainReduction = limiter.getCurrentGainReduction();
        s.limiterActive = limiter.isLimiting();

        s.deviceLost = engine_.isDeviceLost();
        s.monitorLost = engine_.getMonitorOutput().isDeviceLost();

        s.plugins.clear();
        auto latencies = chain.getPluginLatencies();
        for (int i = 0; i < chain.getPluginCount(); ++i) {
            auto* slot = chain.getPluginSlot(i);
            if (slot) {
                AppState::PluginState ps;
                ps.name = slot->name.toStdString();
                ps.bypassed = slot->bypassed;
                ps.loaded = (slot->getProcessor() != nullptr);
                ps.latencySamples = (static_cast<size_t>(i) < latencies.size())
                    ? latencies[static_cast<size_t>(i)].latencySamples : 0;
                // Map slot type to string
                switch (slot->type) {
                    case PluginSlot::Type::BuiltinFilter: ps.type = "builtin_filter"; break;
                    case PluginSlot::Type::BuiltinNoiseRemoval: ps.type = "builtin_noise_removal"; break;
                    case PluginSlot::Type::BuiltinAutoGain: ps.type = "builtin_auto_gain"; break;
                    default: ps.type = "vst"; break;
                }
                s.plugins.push_back(ps);
            }
        }
        if (!s.plugins.empty()) {
            bool anyLoaded = false;
            bool allLoadedBypassed = true;
            for (const auto& ps : s.plugins) {
                if (ps.loaded) {
                    anyLoaded = true;
                    if (!ps.bypassed) { allLoadedBypassed = false; break; }
                }
            }
            s.masterBypassed = anyLoaded && allLoadedBypassed;
        }

        s.chainPDCSamples = chain.getTotalChainPDC();
        double sr = monitor.getSampleRate();
        s.chainPDCMs = (sr > 0.0 && s.chainPDCSamples > 0)
            ? static_cast<float>(s.chainPDCSamples) / static_cast<float>(sr) * 1000.0f : 0.0f;

        if (pm) {
            for (int si = 0; si < numPresetSlots; ++si)
                s.slotNames[static_cast<size_t>(si)] = pm->getSlotName(si).toStdString();
        }
    });
}

} // namespace directpipe
