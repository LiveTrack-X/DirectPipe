// host/Source/Audio/DeviceState.h
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

namespace directpipe {

/**
 * @brief Explicit state machine for audio device connection status.
 *
 * Replaces the implicit combination of 4 boolean flags
 * (deviceLost_, inputDeviceLost_, outputAutoMuted_, attemptingReconnection_)
 * with a single enum that makes all valid states visible.
 *
 * A switch statement over DeviceState forces handling of every state —
 * the compiler warns on missing cases.
 */
enum class DeviceState {
    Running,          ///< Normal operation — all devices active
    InputLost,        ///< Input device lost, output still working (audio callback zeroes input)
    OutputLost,       ///< Output auto-muted due to device loss
    BothLost,         ///< Both input and output lost (full device error)
    Reconnecting,     ///< Actively attempting reconnection
    FallbackDetected  ///< JUCE auto-fallback to wrong device detected
};

/**
 * @brief Events that trigger device state transitions.
 */
enum class DeviceEvent {
    InputError,       ///< Input device error detected
    OutputError,      ///< Output device error detected
    FullError,        ///< Full audio device error
    ReconnectStart,   ///< Reconnection attempt started
    ReconnectSuccess, ///< Device reconnected successfully
    ReconnectFail,    ///< Reconnection attempt failed
    FallbackDetected, ///< JUCE fell back to non-desired device
    UserReset         ///< User manually selected a new device
};

/**
 * @brief Compute the next device state given current state and event.
 *
 * This makes state transitions explicit and auditable. Every valid
 * transition is listed here — no hidden flag combinations.
 */
inline DeviceState transition(DeviceState current, DeviceEvent event)
{
    switch (event) {
        case DeviceEvent::UserReset:
        case DeviceEvent::ReconnectSuccess:
            return DeviceState::Running;

        case DeviceEvent::FullError:
            return DeviceState::BothLost;

        case DeviceEvent::InputError:
            if (current == DeviceState::OutputLost)
                return DeviceState::BothLost;
            return DeviceState::InputLost;

        case DeviceEvent::OutputError:
            if (current == DeviceState::InputLost)
                return DeviceState::BothLost;
            return DeviceState::OutputLost;

        case DeviceEvent::ReconnectStart:
            return DeviceState::Reconnecting;

        case DeviceEvent::ReconnectFail:
            // Return to previous loss state (we don't track which, so use BothLost)
            return DeviceState::BothLost;

        case DeviceEvent::FallbackDetected:
            return DeviceState::FallbackDetected;
    }

    return current;  // Unknown event — no change
}

/**
 * @brief Convert DeviceState to display string (for logging/UI).
 */
inline const char* deviceStateToString(DeviceState state)
{
    switch (state) {
        case DeviceState::Running:          return "Running";
        case DeviceState::InputLost:        return "InputLost";
        case DeviceState::OutputLost:       return "OutputLost";
        case DeviceState::BothLost:         return "BothLost";
        case DeviceState::Reconnecting:     return "Reconnecting";
        case DeviceState::FallbackDetected: return "FallbackDetected";
    }
    return "Unknown";
}

} // namespace directpipe
