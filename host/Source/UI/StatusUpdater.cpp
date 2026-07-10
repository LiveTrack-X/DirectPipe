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

namespace {

const char* boolText(bool value)
{
    return value ? "true" : "false";
}

const char* monitorStatusToString(VirtualCableStatus status)
{
    switch (status) {
        case VirtualCableStatus::NotConfigured: return "NotConfigured";
        case VirtualCableStatus::Active: return "Active";
        case VirtualCableStatus::Error: return "Error";
        case VirtualCableStatus::SampleRateMismatch: return "SampleRateMismatch";
    }
    return "Unknown";
}

juce::String emptyAsNone(const juce::String& value)
{
    return value.isEmpty() ? "(none)" : value;
}

int positiveDelta(int current, int previous)
{
    return current >= previous ? current - previous : current;
}

uint32_t positiveDelta(uint32_t current, uint32_t previous)
{
    return current >= previous ? current - previous : current;
}

uint64_t positiveDelta(uint64_t current, uint64_t previous)
{
    return current >= previous ? current - previous : current;
}

} // namespace

StatusUpdater::StatusUpdater(AudioEngine& engine, StateBroadcaster& broadcaster)
    : engine_(engine), broadcaster_(broadcaster)
{
}

void StatusUpdater::setUI(juce::Label* latencyLabel, juce::Label* cpuLabel, juce::Label* formatLabel,
                          juce::TextButton* inputMuteBtn, juce::TextButton* outputMuteBtn,
                          juce::TextButton* monitorMuteBtn, juce::TextButton* vstMuteBtn,
                          juce::TextButton* panicMuteBtn,
                          juce::Slider* inputGainSlider,
                          LevelMeter* inputMeter, LevelMeter* outputMeter)
{
    latencyLabel_ = latencyLabel;
    cpuLabel_ = cpuLabel;
    formatLabel_ = formatLabel;
    inputMuteBtn_ = inputMuteBtn;
    outputMuteBtn_ = outputMuteBtn;
    monitorMuteBtn_ = monitorMuteBtn;
    vstMuteBtn_ = vstMuteBtn;
    panicMuteBtn_ = panicMuteBtn;
    inputGainSlider_ = inputGainSlider;
    inputMeter_ = inputMeter;
    outputMeter_ = outputMeter;
}

void StatusUpdater::emitAuditDiagnostics(double mainLatencyMs,
                                         double monitorLatencyMs,
                                         bool monitorEnabled,
                                         double cpuPercent,
                                         int recentXruns,
                                         bool limiterActive)
{
    if (!Log::isAuditMode()) {
        auditBaselineValid_ = false;
        auditTicksSinceSnapshot_ = 0;
        return;
    }

    auto& monitor = engine_.getLatencyMonitor();
    auto& router = engine_.getOutputRouter();
    auto& monOut = engine_.getMonitorOutput();

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    engine_.getDeviceManager().getAudioDeviceSetup(setup);
    auto* device = engine_.getDeviceManager().getCurrentAudioDevice();

    const auto deviceState = engine_.getDeviceState();
    const auto monitorStatus = monOut.getStatus();
    const auto callbackOverruns = monitor.getCallbackOverrunCount();
    const uint64_t totalXRunEvents = engine_.getTotalXRunEvents();
    const int monitorDropped = monOut.getDroppedFrames();
    const int monitorUnderruns = monOut.getUnderrunCount();
    const int monitorTrimmed = monOut.getLatencyTrimmedFrames();
    const int monitorFillFrames = monOut.getFillFrames();
    const int monitorTargetFill = monOut.getTargetFillFrames();
    const auto monitorTargetReason = static_cast<monitor_drift::TargetReason>(monOut.getTargetReasonCode());
    const double monitorPlaybackRatio = monOut.getPlaybackRatio();
    const double monitorPllErrorFrames = monOut.getPllErrorFrames();
    const double monitorPllErrorMs = monOut.getPllErrorMs();
    const double monitorPllCorrection = monOut.getPllCorrection();
    const double monitorDriftEstimate = monOut.getDriftEstimate();
    const bool monitorPriming = monOut.isPriming();

    const juce::String actualDriver = device ? device->getTypeName() : engine_.getCurrentDeviceType();
    const juce::String actualDevice = device ? device->getName() : "(none)";
    const juce::String actualMonitorDevice = emptyAsNone(monOut.getActualDeviceName());

    juce::String stateSignature;
    stateSignature
        << deviceStateToString(deviceState)
        << "|" << static_cast<int>(monitorStatus)
        << "|" << actualDriver
        << "|" << setup.inputDeviceName
        << "|" << setup.outputDeviceName
        << "|" << monOut.getDeviceName()
        << "|" << actualMonitorDevice
        << "|" << boolText(engine_.isDeviceLost())
        << "|" << boolText(engine_.isInputDeviceLost())
        << "|" << boolText(engine_.isOutputAutoMuted())
        << "|" << boolText(monOut.isDeviceLost())
        << "|" << boolText(monitorEnabled)
        << "|" << boolText(monOut.isActive())
        << "|" << boolText(engine_.isIpcEnabled());

    ++auditTicksSinceSnapshot_;
    const bool firstSnapshot = !auditBaselineValid_;
    const bool stateChanged = auditBaselineValid_ && stateSignature != auditLastStateSignature_;
    const bool periodicSnapshot = auditTicksSinceSnapshot_ >= kAuditSnapshotTicks;

    if (!firstSnapshot && !stateChanged && !periodicSnapshot)
        return;

    const juce::String reason = firstSnapshot ? "baseline"
        : (stateChanged ? "state-change" : "periodic");

    const uint64_t xrunDelta = firstSnapshot ? 0u : positiveDelta(totalXRunEvents, auditLastTotalXRunEvents_);
    const uint32_t callbackOverrunDelta = firstSnapshot ? 0u
        : positiveDelta(callbackOverruns, auditLastCallbackOverruns_);
    const int monitorDropDelta = firstSnapshot ? 0 : positiveDelta(monitorDropped, auditLastMonitorDroppedFrames_);
    const int monitorUnderrunDelta = firstSnapshot ? 0 : positiveDelta(monitorUnderruns, auditLastMonitorUnderruns_);
    const int monitorTrimDelta = firstSnapshot ? 0 : positiveDelta(monitorTrimmed, auditLastMonitorTrimmedFrames_);

    juce::String audioLine;
    audioLine
        << "Status " << reason
        << ": state=" << deviceStateToString(deviceState)
        << " driver='" << actualDriver << "'"
        << " actualDevice='" << actualDevice << "'"
        << " actualIn='" << emptyAsNone(setup.inputDeviceName) << "'"
        << " actualOut='" << emptyAsNone(setup.outputDeviceName) << "'"
        << " desiredDriver='" << emptyAsNone(engine_.getDesiredDeviceType()) << "'"
        << " desiredIn='" << emptyAsNone(engine_.getDesiredInputDevice()) << "'"
        << " desiredOut='" << emptyAsNone(engine_.getDesiredOutputDevice()) << "'"
        << " sr=" << juce::String(monitor.getSampleRate(), 0)
        << " desiredSR=" << juce::String(engine_.getDesiredSampleRate(), 0)
        << " bs=" << monitor.getBufferSize()
        << " desiredBS=" << engine_.getDesiredBufferSize()
        << " channels=" << engine_.getChannelMode()
        << " latencyMs=" << juce::String(mainLatencyMs, 2)
        << " procMs=" << juce::String(monitor.getProcessingTimeMs(), 3)
        << " cpuPct=" << juce::String(cpuPercent, 1)
        << " xruns60=" << recentXruns
        << " xrunDelta=" << static_cast<juce::int64>(xrunDelta)
        << " callbackOverruns=" << static_cast<int>(callbackOverruns)
        << " callbackOverrunDelta=" << static_cast<int>(callbackOverrunDelta)
        << " inputLost=" << boolText(engine_.isInputDeviceLost())
        << " outputAutoMuted=" << boolText(engine_.isOutputAutoMuted())
        << " inputMuted=" << boolText(engine_.isInputMuted())
        << " outputMuted=" << boolText(engine_.isOutputMuted())
        << " panicMuted=" << boolText(engine_.isMuted())
        << " ipc=" << boolText(engine_.isIpcEnabled())
        << " limiter=" << boolText(limiterActive);
    Log::audit("AUDIO", audioLine);

    juce::String monitorLine;
    monitorLine
        << "Status " << reason
        << ": enabled=" << boolText(monitorEnabled)
        << " active=" << boolText(monOut.isActive())
        << " lost=" << boolText(monOut.isDeviceLost())
        << " status=" << monitorStatusToString(monitorStatus)
        << " desired='" << emptyAsNone(monOut.getDeviceName()) << "'"
        << " actual='" << actualMonitorDevice << "'"
        << " prefBS=" << monOut.getPreferredBufferSize()
        << " actualSR=" << juce::String(monOut.getActualSampleRate(), 0)
        << " actualBS=" << monOut.getActualBufferSize()
        << " latencyMs=" << juce::String(monitorLatencyMs, 2)
        << " volume=" << juce::String(router.getVolume(OutputRouter::Output::Monitor), 3)
        << " level=" << juce::String(router.getLevel(OutputRouter::Output::Monitor), 3)
        << " fillFrames=" << monitorFillFrames
        << " targetFill=" << monitorTargetFill
        << " targetReason=" << monitor_drift::targetReasonToString(monitorTargetReason)
        << " playbackRatio=" << juce::String(monitorPlaybackRatio, 7)
        << " pllErrorFrames=" << juce::String(monitorPllErrorFrames, 2)
        << " pllErrorMs=" << juce::String(monitorPllErrorMs, 3)
        << " pllCorrection=" << juce::String(monitorPllCorrection, 8)
        << " driftEstimate=" << juce::String(monitorDriftEstimate, 4)
        << " priming=" << boolText(monitorPriming)
        << " droppedFrames=" << monitorDropped
        << " droppedFramesDelta=" << monitorDropDelta
        << " underruns=" << monitorUnderruns
        << " underrunDelta=" << monitorUnderrunDelta
        << " trimmedFrames=" << monitorTrimmed
        << " emergencyTrimDelta=" << monitorTrimDelta
        << " producerBlock=" << monOut.getProducerBlockSize()
        << " consumerBlock=" << monOut.getConsumerBlockSize()
        << " sampleRate=" << juce::String(monOut.getActualSampleRate(), 0)
        << " capacityFrames=" << monOut.getCapacityFrames();
    Log::audit("MONITOR", monitorLine);

    auditBaselineValid_ = true;
    auditTicksSinceSnapshot_ = 0;
    auditLastStateSignature_ = stateSignature;
    auditLastTotalXRunEvents_ = totalXRunEvents;
    auditLastCallbackOverruns_ = callbackOverruns;
    auditLastMonitorDroppedFrames_ = monitorDropped;
    auditLastMonitorUnderruns_ = monitorUnderruns;
    auditLastMonitorTrimmedFrames_ = monitorTrimmed;
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
    // Color scheme:
    //   INPUT:       GREEN (active) / RED (muted) - independent of panic
    //   OUT/MON/VST: GREEN (active) / RED (user muted) / TONED-DOWN LOCK-RED (panic locked)
    //   PANIC MUTE:  RED (ready) / GREEN + "UNMUTE" (panic active)
    {
        // INPUT button — 2 states, independent of panic
        bool inMuted = engine_.isInputMuted();
        if (inMuted != cachedInputMuted_) {
            cachedInputMuted_ = inMuted;
            inputMuteBtn_->setColour(juce::TextButton::buttonColourId,
                inMuted ? juce::Colour(0xFFE05050) : juce::Colour(0xFF4CAF50));
        }

        // Panic state affects OUT/MON/VST buttons
        bool outMuted = engine_.isOutputMuted();
        bool monUserMuted = !engine_.getOutputRouter().isEnabled(OutputRouter::Output::Monitor);
        bool vstActive = engine_.isIpcEnabled();

        if (muted != cachedPanicActive_ || outMuted != cachedOutputMuted_ ||
            monUserMuted != cachedMonitorMuted_ || vstActive != cachedVstEnabled_)
        {
            cachedPanicActive_ = muted;
            cachedOutputMuted_ = outMuted;
            cachedMonitorMuted_ = monUserMuted;
            cachedVstEnabled_ = vstActive;

            // OUT button — 3 states
            if (muted) {
                outputMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFD46161));
                outputMuteBtn_->setEnabled(true); // locked by action guard during panic
            } else if (outMuted) {
                outputMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
                outputMuteBtn_->setEnabled(true);
            } else {
                outputMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF4CAF50));
                outputMuteBtn_->setEnabled(true);
            }

            // MON button — 3 states
            if (muted) {
                monitorMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFD46161));
                monitorMuteBtn_->setEnabled(true); // locked by action guard during panic
            } else if (monUserMuted) {
                monitorMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
                monitorMuteBtn_->setEnabled(true);
            } else {
                monitorMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF4CAF50));
                monitorMuteBtn_->setEnabled(true);
            }

            // VST button — 3 states (note: vstActive means IPC enabled = active)
            if (muted) {
                vstMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFD46161));
                vstMuteBtn_->setEnabled(true); // locked by action guard during panic
            } else if (!vstActive) {
                vstMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
                vstMuteBtn_->setEnabled(true);
            } else {
                vstMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF4CAF50));
                vstMuteBtn_->setEnabled(true);
            }

            // PANIC MUTE button
            if (panicMuteBtn_) {
                if (muted) {
                    panicMuteBtn_->setButtonText("UNMUTE");
                    panicMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF4CAF50));
                } else {
                    panicMuteBtn_->setButtonText("PANIC MUTE");
                    panicMuteBtn_->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFFE05050));
                }
            }
        }
    }

    // ── Latency label ──
    double mainLatency = monitor.getTotalLatencyVirtualMicMs();
    auto& monOut = engine_.getMonitorOutput();
    auto& router = engine_.getOutputRouter();
    bool monEnabled = router.isEnabled(OutputRouter::Output::Monitor);
    double monitorLatency = 0.0;

    {
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
    double cpuPct = monitor.getCpuUsagePercent();
    int xruns = engine_.getRecentXRunCount();
    bool limActive = engine_.getSafetyLimiter().isLimiting();
    {
        engine_.updateXRunTracking();
        cpuPct = monitor.getCpuUsagePercent();
        xruns = engine_.getRecentXRunCount();
        limActive = engine_.getSafetyLimiter().isLimiting();
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
    emitAuditDiagnostics(mainLatency, monitorLatency, monEnabled, cpuPct, xruns, limActive);

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
        s.inputMuted = engine_.isInputMuted();
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
            s.activeSlot = slot;  // 0-5 or -1, no clamping
            s.autoSlotActive = (slot == 5);  // backward compat (deprecated)
        }
        s.recording = engine_.getRecorder().isRecording();
        s.recordingSeconds = engine_.getRecorder().getRecordedSeconds();
        s.ipcEnabled = engine_.isIpcEnabled();
        s.xrunCount = engine_.getRecentXRunCount();

        auto& limiter = engine_.getSafetyLimiter();
        s.limiterEnabled = limiter.isEnabled();
        s.limiterCeilingdB = limiter.getCeilingdB();
        s.safetyHeadroomEnabled = engine_.isSafetyHeadroomEnabled();
        s.safetyHeadroomdB = engine_.getSafetyHeadroomdB();
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
