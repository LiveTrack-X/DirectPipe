// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>

namespace directpipe::audio_settings_detail {

enum class DeviceDirection {
    Input,
    Output
};

struct RecoverySelection {
    int placeholderId = 0;
    int desiredDeviceId = 0;
    juce::String placeholderText;
    bool desiredDeviceAvailable = false;
};

inline bool needsRecoveryPlaceholder(DeviceDirection direction,
                                     bool isAsio,
                                     bool inputLost,
                                     bool outputLost)
{
    if (isAsio)
        return inputLost || outputLost;

    return direction == DeviceDirection::Input ? inputLost : outputLost;
}

inline RecoverySelection makeRecoverySelection(
    const juce::String& desiredDevice,
    const juce::StringArray& availableDevices,
    int firstDeviceId)
{
    RecoverySelection result;
    if (desiredDevice.isEmpty())
        return result;

    const auto desiredIndex = availableDevices.indexOf(desiredDevice);
    result.desiredDeviceAvailable = desiredIndex >= 0;
    result.desiredDeviceId = result.desiredDeviceAvailable
        ? desiredIndex + firstDeviceId
        : 0;
    result.placeholderId = availableDevices.size() + firstDeviceId;
    result.placeholderText = desiredDevice
        + (result.desiredDeviceAvailable
               ? " (Reconnect)"
               : " (Disconnected)");
    return result;
}

inline bool isRecoveryPlaceholderText(const juce::String& text)
{
    return text.endsWith("(Reconnect)")
        || text.endsWith("(Disconnected)");
}

} // namespace directpipe::audio_settings_detail
