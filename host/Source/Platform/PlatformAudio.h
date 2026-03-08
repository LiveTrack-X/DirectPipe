// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file PlatformAudio.h
 * @brief Platform-specific audio device type helpers
 *
 * Abstracts WASAPI/CoreAudio/ALSA device type names so the rest of
 * the codebase doesn't hardcode Windows-specific strings.
 */
#pragma once

#include <JuceHeader.h>

namespace directpipe {
namespace PlatformAudio {

/**
 * @brief Get the default shared-mode audio device type name for this platform.
 *
 * Windows: "Windows Audio" (WASAPI Shared)
 * macOS:   "CoreAudio"
 * Linux:   "ALSA"
 */
inline juce::String getDefaultSharedDeviceType()
{
#if JUCE_WINDOWS
    return "Windows Audio";
#elif JUCE_MAC
    return "CoreAudio";
#elif JUCE_LINUX
    return "ALSA";
#else
    return {};
#endif
}

/**
 * @brief Check if a device type name is a shared/non-exclusive audio type
 *        suitable for Monitor output (i.e. NOT ASIO or other exclusive types).
 */
inline bool isSharedDeviceType(const juce::String& typeName)
{
#if JUCE_WINDOWS
    return typeName.containsIgnoreCase("Windows Audio")
        || typeName.containsIgnoreCase("DirectSound")
        || typeName.containsIgnoreCase("WASAPI");
#elif JUCE_MAC
    return typeName.containsIgnoreCase("CoreAudio");
#elif JUCE_LINUX
    return typeName.containsIgnoreCase("ALSA")
        || typeName.containsIgnoreCase("JACK");
#else
    juce::ignoreUnused(typeName);
    return false;
#endif
}

/**
 * @brief Check if a device type name is an exclusive single-device driver (e.g. ASIO).
 */
inline bool isExclusiveDriverType(const juce::String& typeName)
{
    return typeName.containsIgnoreCase("ASIO");
}

/**
 * @brief Get shared-mode output devices from the device manager, regardless of current driver.
 *
 * Scans all available device types and returns output device names from the first
 * shared-mode type that has devices. Useful for Monitor output device enumeration.
 */
inline juce::StringArray getSharedModeOutputDevices(juce::AudioDeviceManager& dm)
{
    for (auto* type : dm.getAvailableDeviceTypes()) {
        if (isSharedDeviceType(type->getTypeName())) {
            type->scanForDevices();
            auto devices = type->getDeviceNames(false);
            if (devices.size() > 0)
                return devices;
        }
    }
    return {};
}

} // namespace PlatformAudio
} // namespace directpipe
