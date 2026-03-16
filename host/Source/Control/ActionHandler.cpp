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
 * @file ActionHandler.cpp
 * @brief ActionEvent routing implementation
 */

#include "ActionHandler.h"
#include "Log.h"
#include "../Audio/AudioEngine.h"
#include "../UI/PresetSlotBar.h"
#include "../UI/PresetManager.h"
#include "../Util/ScopedGuard.h"

namespace directpipe {

ActionHandler::ActionHandler(AudioEngine& engine, PresetManager& presetMgr,
                             PresetSlotBar& slotBar,
                             std::atomic<bool>& loadingSlot,
                             std::atomic<bool>& partialLoad)
    : engine_(engine),
      presetMgr_(presetMgr),
      slotBar_(slotBar),
      loadingSlot_(loadingSlot),
      partialLoad_(partialLoad)
{
}

// ─── Panic Mute (shared logic) ───────────────────────────────────────────────

void ActionHandler::doPanicMute(bool mute)
{
    engine_.setMuted(mute);
    auto& router = engine_.getOutputRouter();
    if (mute) {
        preMuteMonitorEnabled_ = router.isEnabled(OutputRouter::Output::Monitor);
        preMuteOutputMuted_ = engine_.isOutputMuted();
        preMuteVstEnabled_ = engine_.isIpcEnabled();
        engine_.setOutputMuted(false);
        router.setEnabled(OutputRouter::Output::Monitor, false);
        engine_.setMonitorEnabled(false);
        if (preMuteVstEnabled_) engine_.setIpcEnabled(false);
        auto& recorder = engine_.getRecorder();
        if (recorder.isRecording()) {
            auto lastFile = recorder.getRecordingFile();
            recorder.stopRecording();
            if (onRecordingStopped) onRecordingStopped(lastFile);
        }
    } else {
        bool restoreMuted = engine_.isOutputNone() ? true : preMuteOutputMuted_;
        engine_.setOutputMuted(restoreMuted);
        router.setEnabled(OutputRouter::Output::Monitor, preMuteMonitorEnabled_);
        engine_.setMonitorEnabled(preMuteMonitorEnabled_);
        if (preMuteVstEnabled_) engine_.setIpcEnabled(true);
    }
    Log::info("ACTION", "Panic mute " + juce::String(mute ? "engaged" : "disengaged")
        + " — pre-mute state: monitor=" + juce::String(preMuteMonitorEnabled_ ? "on" : "off")
        + ", outputMuted=" + juce::String(preMuteOutputMuted_ ? "yes" : "no")
        + ", vstEnabled=" + juce::String(preMuteVstEnabled_ ? "yes" : "no"));
    if (onIpcStateChanged) onIpcStateChanged(engine_.isIpcEnabled());
    if (onPanicStateChanged) onPanicStateChanged(mute);
    if (onDirty) onDirty();
}

void ActionHandler::restorePanicMuteFromSettings()
{
    if (!engine_.isMuted()) return;
    auto& router = engine_.getOutputRouter();
    preMuteMonitorEnabled_ = router.isEnabled(OutputRouter::Output::Monitor);
    preMuteOutputMuted_ = engine_.isOutputMuted();
    preMuteVstEnabled_ = engine_.isIpcEnabled();
    engine_.setOutputMuted(false);
    router.setEnabled(OutputRouter::Output::Monitor, false);
    engine_.setMonitorEnabled(false);
    if (preMuteVstEnabled_) engine_.setIpcEnabled(false);
}

// ─── Button Toggle Methods ───────────────────────────────────────────────────

void ActionHandler::toggleOutputMute()
{
    if (engine_.isMuted()) return;
    if (engine_.isOutputNone()) return;
    bool outputMuted = !engine_.isOutputMuted();
    engine_.setOutputMuted(outputMuted);
    engine_.clearOutputAutoMute();
    if (onDirty) onDirty();
}

void ActionHandler::toggleMonitorMute()
{
    if (engine_.isMuted()) return;
    auto& router = engine_.getOutputRouter();
    bool enabled = !router.isEnabled(OutputRouter::Output::Monitor);
    router.setEnabled(OutputRouter::Output::Monitor, enabled);
    engine_.setMonitorEnabled(enabled);
    if (onDirty) onDirty();
}

void ActionHandler::toggleIpcMute()
{
    if (engine_.isMuted()) return;
    bool enabled = !engine_.isIpcEnabled();
    engine_.setIpcEnabled(enabled);
    if (onIpcStateChanged) onIpcStateChanged(enabled);
    if (onDirty) onDirty();
}

void ActionHandler::togglePanicMute()
{
    doPanicMute(!engine_.isMuted());
}

// ─── Action Dispatch ─────────────────────────────────────────────────────────

void ActionHandler::handle(const ActionEvent& event)
{
    switch (event.action) {
        case Action::PluginBypass: {
            if (engine_.isMuted()) break;
            engine_.getVSTChain().togglePluginBypassed(event.intParam);
            break;
        }

        case Action::MasterBypass: {
            if (engine_.isMuted()) break;
            bool anyActive = false;
            for (int i = 0; i < engine_.getVSTChain().getPluginCount(); ++i) {
                if (!engine_.getVSTChain().isPluginBypassed(i)) { anyActive = true; break; }
            }
            {
                AtomicGuard loadGuard(loadingSlot_);
                for (int i = 0; i < engine_.getVSTChain().getPluginCount(); ++i)
                    engine_.getVSTChain().setPluginBypassed(i, anyActive);
            }
            int activeSlot = presetMgr_.getActiveSlot();
            if (activeSlot >= 0 && !partialLoad_.load())
                presetMgr_.saveSlot(activeSlot);
            if (onDirty) onDirty();
            break;
        }

        case Action::ToggleMute: {
            if (event.stringParam == "monitor") {
                if (engine_.isMuted()) break;
                auto& router = engine_.getOutputRouter();
                bool enabled = !router.isEnabled(OutputRouter::Output::Monitor);
                router.setEnabled(OutputRouter::Output::Monitor, enabled);
                engine_.setMonitorEnabled(enabled);
            } else if (event.stringParam == "output") {
                if (engine_.isMuted()) break;
                if (engine_.isOutputNone()) break;
                bool outputMuted = !engine_.isOutputMuted();
                engine_.setOutputMuted(outputMuted);
                engine_.clearOutputAutoMute();
            } else {
                doPanicMute(!engine_.isMuted());
                return;  // doPanicMute already calls onDirty
            }
            if (onDirty) onDirty();
            break;
        }

        case Action::PanicMute:
        case Action::InputMuteToggle:
            doPanicMute(!engine_.isMuted());
            break;

        case Action::InputGainAdjust:
            if (engine_.isMuted()) break;
            engine_.setInputGain(engine_.getInputGain() + event.floatParam * 0.1f);
            if (onInputGainSync) onInputGainSync(engine_.getInputGain());
            if (onDirty) onDirty();
            break;

        case Action::SetVolume:
            if (event.stringParam == "monitor") {
                if (engine_.isMuted()) break;
                engine_.getOutputRouter().setVolume(OutputRouter::Output::Monitor, event.floatParam);
                if (onDirty) onDirty();
            } else if (event.stringParam == "input") {
                if (engine_.isMuted()) break;
                engine_.setInputGain(event.floatParam);
                if (onInputGainSync) onInputGainSync(event.floatParam);
                if (onDirty) onDirty();
            } else if (event.stringParam == "output") {
                if (engine_.isMuted()) break;
                engine_.getOutputRouter().setVolume(OutputRouter::Output::Main, event.floatParam);
                if (onDirty) onDirty();
            }
            break;

        case Action::MonitorToggle: {
            if (engine_.isMuted()) break;
            auto& router = engine_.getOutputRouter();
            bool enabled = !router.isEnabled(OutputRouter::Output::Monitor);
            router.setEnabled(OutputRouter::Output::Monitor, enabled);
            engine_.setMonitorEnabled(enabled);
            if (onDirty) onDirty();
            break;
        }

        case Action::RecordingToggle: {
            if (engine_.isMuted()) break;
            auto& recorder = engine_.getRecorder();
            if (recorder.isRecording()) {
                auto lastFile = recorder.getRecordingFile();
                recorder.stopRecording();
                if (onRecordingStopped) onRecordingStopped(lastFile);
            } else {
                auto& monitor = engine_.getLatencyMonitor();
                double sr = monitor.getSampleRate();
                if (sr <= 0.0) {
                    juce::Logger::writeToLog("Recording: no audio device active");
                    break;
                }
                auto timestamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
                auto dir = getRecordingFolder ? getRecordingFolder()
                    : juce::File::getSpecialLocation(
                        juce::File::userDocumentsDirectory).getChildFile("DirectPipe Recordings");
                dir.createDirectory();
                auto file = dir.getChildFile("DirectPipe_" + timestamp + ".wav");
                if (!recorder.startRecording(file, sr, 2))
                    if (onNotification) onNotification("Recording failed - check folder permissions",
                                                        NotificationLevel::Error);
            }
            break;
        }

        case Action::SetPluginParameter: {
            if (engine_.isMuted()) break;
            engine_.getVSTChain().setPluginParameter(
                event.intParam, event.intParam2, event.floatParam);
            break;
        }

        case Action::IpcToggle: {
            if (engine_.isMuted()) break;
            bool enabled = !engine_.isIpcEnabled();
            engine_.setIpcEnabled(enabled);
            if (onIpcStateChanged) onIpcStateChanged(enabled);
            if (onDirty) onDirty();
            break;
        }

        case Action::XRunReset:
            engine_.requestXRunReset();
            break;

        case Action::LoadPreset:
        case Action::SwitchPresetSlot:
        case Action::NextPreset:
        case Action::PreviousPreset:
            if (engine_.isMuted()) break;
            slotBar_.handlePresetAction(event);
            break;

        default:
            break;
    }
}

} // namespace directpipe
