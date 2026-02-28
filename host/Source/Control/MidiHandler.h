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
 * @file MidiHandler.h
 * @brief MIDI CC mapping and Learn mode handler
 *
 * Receives MIDI input from controllers (nanoKONTROL, MIDI keyboards, etc.)
 * and maps CC messages to DirectPipe actions.
 */
#pragma once

#include <JuceHeader.h>
#include "ActionDispatcher.h"
#include "ControlMapping.h"

#include <mutex>
#include <vector>
#include <functional>

namespace directpipe {

/// MIDI mapping types
enum class MidiMappingType {
    Toggle,      ///< CC >= 64 → ON, < 64 → OFF
    Momentary,   ///< CC >= 64 → ON while held
    Continuous,  ///< CC 0~127 → 0.0~1.0
    NoteOnOff,   ///< Note ON → toggle
};

/// A single MIDI-to-action mapping
struct MidiBinding {
    int cc = -1;                 ///< CC number (-1 = note mode)
    int note = -1;               ///< Note number (-1 = CC mode)
    int channel = 0;             ///< MIDI channel (0 = any, 1-16 = specific)
    MidiMappingType type = MidiMappingType::Toggle;
    ActionEvent action;
    std::string deviceName;      ///< MIDI device this binding applies to ("" = any)
    bool lastState = false;      ///< For toggle tracking
};

/**
 * @brief Handles MIDI input and maps to control actions.
 *
 * Features:
 * - MIDI Learn mode for easy mapping
 * - Multiple MIDI device support
 * - Hot-plug detection
 * - LED feedback for controllers with LEDs
 */
class MidiHandler : public juce::MidiInputCallback {
public:
    explicit MidiHandler(ActionDispatcher& dispatcher);
    ~MidiHandler() override;

    /**
     * @brief Initialize MIDI input and start listening.
     */
    void initialize();

    /**
     * @brief Shut down MIDI handling.
     */
    void shutdown();

    /**
     * @brief Get list of available MIDI input devices.
     */
    juce::StringArray getAvailableDevices() const;

    /**
     * @brief Open a MIDI input device.
     * @param deviceName Device to open.
     * @return true if opened successfully.
     */
    bool openDevice(const juce::String& deviceName);

    /**
     * @brief Close all MIDI input devices.
     */
    void closeAllDevices();

    /**
     * @brief Add a MIDI mapping.
     */
    void addBinding(const MidiBinding& binding);

    /**
     * @brief Remove a MIDI mapping by index.
     */
    void removeBinding(int index);

    /**
     * @brief Get all MIDI bindings.
     */
    std::vector<MidiBinding> getBindings() const {
        std::lock_guard<std::mutex> lock(bindingsMutex_);
        return bindings_;
    }

    /**
     * @brief Start MIDI Learn mode.
     *
     * The next MIDI CC/Note message received will be captured
     * and reported via the callback.
     *
     * @param callback Called with captured CC/Note info.
     */
    void startLearn(std::function<void(int cc, int note, int channel,
                                        const juce::String& deviceName)> callback);

    /**
     * @brief Cancel MIDI Learn mode.
     */
    void stopLearn();

    /**
     * @brief Check if currently in Learn mode.
     */
    bool isLearning() const { return learning_; }

    /**
     * @brief Load bindings from mapping config.
     */
    void loadFromMappings(const std::vector<MidiMapping>& mappings);

    /**
     * @brief Export current bindings to mapping format.
     */
    std::vector<MidiMapping> exportMappings() const;

    /**
     * @brief Send MIDI feedback (for LED controllers).
     */
    void sendFeedback(int cc, int channel, int value);

    /**
     * @brief Rescan for MIDI devices (hot-plug).
     */
    void rescanDevices();

private:
    // juce::MidiInputCallback
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    void processCC(int cc, int channel, int value, const juce::String& deviceName);
    void processNote(int note, int channel, bool noteOn, const juce::String& deviceName);

    ActionDispatcher& dispatcher_;
    mutable std::mutex bindingsMutex_;
    std::vector<MidiBinding> bindings_;

    std::vector<std::unique_ptr<juce::MidiInput>> openInputs_;
    std::unique_ptr<juce::MidiOutput> midiOutput_;  // For LED feedback

    bool learning_ = false;
    std::function<void(int, int, int, const juce::String&)> learnCallback_;
};

} // namespace directpipe
