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
 * @file MidiHandler.cpp
 * @brief MIDI CC handler implementation
 */

#include "MidiHandler.h"
#include "Log.h"

namespace directpipe {

MidiHandler::MidiHandler(ActionDispatcher& dispatcher)
    : dispatcher_(dispatcher)
{
}

MidiHandler::~MidiHandler()
{
    shutdown();
}

void MidiHandler::initialize()
{
    // Open all available MIDI devices
    auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& device : devices) {
        auto input = juce::MidiInput::openDevice(device.identifier, this);
        if (input) {
            input->start();
            openInputs_.push_back(std::move(input));
            juce::Logger::writeToLog("[MIDI] Opened: " + device.name);
        }
    }
}

void MidiHandler::shutdown()
{
    alive_->store(false, std::memory_order_release);

    // Stop learn timer safely — stopTimer() is thread-safe, but destroying
    // a Timer (reset) must happen on the message thread. Just stop it here;
    // the destructor handles cleanup on the message thread.
    if (learnTimer_)
        learnTimer_->stopTimer();
    learning_.store(false, std::memory_order_release);

    for (auto& input : openInputs_) {
        input->stop();
    }
    openInputs_.clear();
    {
        std::lock_guard<std::mutex> lock(bindingsMutex_);
        learnCallback_ = nullptr;
        midiOutput_.reset();
    }
}

juce::StringArray MidiHandler::getAvailableDevices() const
{
    juce::StringArray names;
    for (const auto& device : juce::MidiInput::getAvailableDevices()) {
        names.add(device.name);
    }
    return names;
}

bool MidiHandler::openDevice(const juce::String& deviceName)
{
    auto devices = juce::MidiInput::getAvailableDevices();
    for (const auto& device : devices) {
        if (device.name == deviceName) {
            auto input = juce::MidiInput::openDevice(device.identifier, this);
            if (input) {
                input->start();
                openInputs_.push_back(std::move(input));
                return true;
            }
        }
    }
    return false;
}

void MidiHandler::closeAllDevices()
{
    for (auto& input : openInputs_) {
        input->stop();
    }
    openInputs_.clear();
}

void MidiHandler::addBinding(const MidiBinding& binding)
{
    std::lock_guard<std::mutex> lock(bindingsMutex_);
    for (const auto& existing : bindings_) {
        if (existing.cc == binding.cc && existing.channel == binding.channel
            && existing.cc >= 0) {
            juce::Logger::writeToLog("[MIDI] Warning: duplicate CC" + juce::String(binding.cc)
                + " ch" + juce::String(binding.channel) + " binding");
        }
    }
    bindings_.push_back(binding);
}

void MidiHandler::removeBinding(int index)
{
    std::lock_guard<std::mutex> lock(bindingsMutex_);
    if (index >= 0 && static_cast<size_t>(index) < bindings_.size()) {
        bindings_.erase(bindings_.begin() + index);
    }
}

void MidiHandler::startLearn(
    std::function<void(int, int, int, const juce::String&)> callback)
{
    {
        std::lock_guard<std::mutex> lock(bindingsMutex_);
        learnCallback_ = std::move(callback);
    }
    learning_.store(true, std::memory_order_release);

    // 30-second timeout to prevent infinite waiting
    struct LearnTimeout : juce::Timer {
        MidiHandler& handler;
        LearnTimeout(MidiHandler& h) : handler(h) {}
        void timerCallback() override {
            stopTimer();  // Stop BEFORE any cleanup — safe to call on self
            Log::info("MIDI", "Learn timed out after 30s");
            // Clear learn state directly — do NOT call stopLearn() which would
            // call learnTimer_.reset() and destroy us while on the call stack.
            handler.learning_.store(false, std::memory_order_release);
            {
                std::lock_guard<std::mutex> lock(handler.bindingsMutex_);
                handler.learnCallback_ = nullptr;
            }
            // Schedule our own cleanup after this callback returns safely
            auto aliveFlag = handler.alive_;
            juce::MessageManager::callAsync([aliveFlag, &h = handler]() {
                if (!aliveFlag->load(std::memory_order_acquire)) return;
                h.learnTimer_.reset();
            });
        }
    };
    learnTimer_ = std::make_unique<LearnTimeout>(*this);
    static_cast<LearnTimeout*>(learnTimer_.get())->startTimer(30000);

    juce::Logger::writeToLog("[MIDI] Learn started");
}

void MidiHandler::stopLearn()
{
    jassert(juce::MessageManager::getInstance()->isThisTheMessageThread());

    if (learnTimer_) {
        learnTimer_->stopTimer();
        learnTimer_.reset();
    }
    learning_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(bindingsMutex_);
        learnCallback_ = nullptr;
    }
    juce::Logger::writeToLog("[MIDI] Learn stopped");
}

void MidiHandler::handleIncomingMidiMessage(
    juce::MidiInput* source, const juce::MidiMessage& message)
{
    // ─── MIDI Callback Thread ───────────────────────────────────────────
    // 이 메서드는 JUCE MIDI callback 스레드에서 호출됨 (Message thread 아님!)
    // WARNING: bindingsMutex_ 내에서 dispatch 금지 — 매칭 결과를 로컬 벡터에 수집 후
    // 락 해제한 다음 ActionDispatcher를 통해 디스패치 (교착 방지)
    // ────────────────────────────────────────────────────────────────────

    juce::String deviceName = source ? source->getName() : "";

    if (message.isController()) {
        int cc = message.getControllerNumber();
        int channel = message.getChannel();
        int value = message.getControllerValue();

        {
            // CAS (compare-and-swap) ensures only one MIDI message wins the learn slot.
            // Multiple devices can fire simultaneously on different callback threads.
            // DO NOT simplify to if(learning_.load()) { learning_.store(false); ... }
            // — that has a TOCTOU race where two threads both read true and both proceed.
            bool expected = true;
            if (learning_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
                std::function<void(int, int, int, const juce::String&)> cb;
                {
                    std::lock_guard<std::mutex> lock(bindingsMutex_);
                    cb = std::move(learnCallback_);
                    learnCallback_ = nullptr;
                }
                if (cb) cb(cc, -1, channel, deviceName);
                juce::Logger::writeToLog("[MIDI] Learn: CC#" + juce::String(cc) + " ch" + juce::String(channel));
                return;
            }
        }

        processCC(cc, channel, value, deviceName);
    }
    else if (message.isNoteOn() || message.isNoteOff()) {
        int note = message.getNoteNumber();
        int channel = message.getChannel();
        bool noteOn = message.isNoteOn();

        if (noteOn) {
            bool expected = true;
            if (learning_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
                std::function<void(int, int, int, const juce::String&)> cb;
                {
                    std::lock_guard<std::mutex> lock(bindingsMutex_);
                    cb = std::move(learnCallback_);
                    learnCallback_ = nullptr;
                }
                if (cb) cb(-1, note, channel, deviceName);
                juce::Logger::writeToLog("[MIDI] Learn: Note#" + juce::String(note) + " ch" + juce::String(channel));
                return;
            }
        }

        processNote(note, channel, noteOn, deviceName);
    }
}

void MidiHandler::processCC(int cc, int channel, int value,
                              const juce::String& deviceName)
{
    std::vector<ActionEvent> pendingActions;

    {
        std::lock_guard<std::mutex> lock(bindingsMutex_);
        for (auto& binding : bindings_) {
            if (binding.cc != cc) continue;
            if (binding.channel != 0 && binding.channel != channel) continue;
            if (!binding.deviceName.empty() &&
                binding.deviceName != deviceName.toStdString()) continue;

            switch (binding.type) {
                case MidiMappingType::Toggle: {
                    bool newState = (value >= 64);
                    if (newState != binding.lastState) {
                        binding.lastState = newState;
                        if (newState)
                            pendingActions.push_back(binding.action);
                    }
                    break;
                }
                case MidiMappingType::Momentary: {
                    if (value >= 64)
                        pendingActions.push_back(binding.action);
                    break;
                }
                case MidiMappingType::Continuous: {
                    auto event = binding.action;
                    float normalized = static_cast<float>(value) / 127.0f;  // 0.0 - 1.0

                    if (event.action == Action::InputGainAdjust) {
                        // Continuous CC for input gain: map 0-127 → absolute gain 0.0-2.0
                        // Use SetVolume with "input" target for absolute set (bypasses * 0.1f delta)
                        event.action = Action::SetVolume;
                        event.stringParam = "input";
                        event.floatParam = normalized * 2.0f;  // 0.0 - 2.0 range
                    } else {
                        event.floatParam = normalized;
                    }

                    pendingActions.push_back(event);
                    break;
                }
                default:
                    break;
            }
        }
    }

    for (auto& event : pendingActions) {
        if (event.action != Action::SetVolume &&
            event.action != Action::InputGainAdjust &&
            event.action != Action::SetPluginParameter)
            juce::Logger::writeToLog("[MIDI] CC#" + juce::String(cc) + " ch" + juce::String(channel) + " -> " + actionToString(event.action));
        dispatcher_.dispatch(event);
    }
}

void MidiHandler::processNote(int note, int channel, bool noteOn,
                                const juce::String& deviceName)
{
    if (!noteOn) return;

    std::vector<ActionEvent> pendingActions;

    {
        std::lock_guard<std::mutex> lock(bindingsMutex_);
        for (auto& binding : bindings_) {
            if (binding.type != MidiMappingType::NoteOnOff) continue;
            if (binding.note != note) continue;
            if (binding.channel != 0 && binding.channel != channel) continue;
            if (!binding.deviceName.empty() &&
                binding.deviceName != deviceName.toStdString()) continue;

            pendingActions.push_back(binding.action);
        }
    }

    for (auto& event : pendingActions) {
        juce::Logger::writeToLog("[MIDI] Note#" + juce::String(note) + " ch" + juce::String(channel) + " -> " + actionToString(event.action));
        dispatcher_.dispatch(event);
    }
}

void MidiHandler::loadFromMappings(const std::vector<MidiMapping>& mappings)
{
    std::lock_guard<std::mutex> lock(bindingsMutex_);
    bindings_.clear();
    for (const auto& m : mappings) {
        MidiBinding b;
        b.cc = m.cc;
        b.note = m.note;
        b.channel = m.channel;
        b.type = m.type;
        b.action = m.action;
        b.deviceName = m.deviceName;
        bindings_.push_back(b);
    }
}

std::vector<MidiMapping> MidiHandler::exportMappings() const
{
    std::lock_guard<std::mutex> lock(bindingsMutex_);
    std::vector<MidiMapping> mappings;
    for (const auto& b : bindings_) {
        MidiMapping m;
        m.cc = b.cc;
        m.note = b.note;
        m.channel = b.channel;
        m.type = b.type;
        m.action = b.action;
        m.deviceName = b.deviceName;
        mappings.push_back(m);
    }
    return mappings;
}

void MidiHandler::sendFeedback(int cc, int channel, int value)
{
    std::lock_guard<std::mutex> lock(bindingsMutex_);
    if (midiOutput_) {
        midiOutput_->sendMessageNow(
            juce::MidiMessage::controllerEvent(channel, cc, value));
    }
}

void MidiHandler::rescanDevices()
{
    closeAllDevices();
    initialize();
}

void MidiHandler::injectTestMessage(const juce::MidiMessage& message)
{
    handleIncomingMidiMessage(nullptr, message);
}

} // namespace directpipe
