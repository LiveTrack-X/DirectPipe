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
            juce::Logger::writeToLog("MIDI opened: " + device.name);
        }
    }
}

void MidiHandler::shutdown()
{
    for (auto& input : openInputs_) {
        input->stop();
    }
    openInputs_.clear();
    {
        std::lock_guard<std::mutex> lock(bindingsMutex_);
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
}

void MidiHandler::stopLearn()
{
    learning_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(bindingsMutex_);
        learnCallback_ = nullptr;
    }
}

void MidiHandler::handleIncomingMidiMessage(
    juce::MidiInput* source, const juce::MidiMessage& message)
{
    juce::String deviceName = source ? source->getName() : "";

    if (message.isController()) {
        int cc = message.getControllerNumber();
        int channel = message.getChannel();
        int value = message.getControllerValue();

        {
            bool expected = true;
            if (learning_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
                std::function<void(int, int, int, const juce::String&)> cb;
                {
                    std::lock_guard<std::mutex> lock(bindingsMutex_);
                    cb = std::move(learnCallback_);
                    learnCallback_ = nullptr;
                }
                if (cb) cb(cc, -1, channel, deviceName);
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
                    event.floatParam = static_cast<float>(value) / 127.0f;
                    pendingActions.push_back(event);
                    break;
                }
                default:
                    break;
            }
        }
    }

    for (auto& event : pendingActions)
        dispatcher_.dispatch(event);
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

    for (auto& event : pendingActions)
        dispatcher_.dispatch(event);
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

} // namespace directpipe
