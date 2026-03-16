// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file Log.cpp
 * @brief Session header and structured logging implementation
 */

#include "Log.h"

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace directpipe {
namespace Log {

void sessionStart(const juce::String& version)
{
    info("APP", "DirectPipe v" + version + " started");
    info("APP", "─────────────────────────────────────────");

    // OS info
#if JUCE_WINDOWS
    {
        OSVERSIONINFOEXW osvi{};
        osvi.dwOSVersionInfoSize = sizeof(osvi);

        // Use RtlGetVersion (ntdll) — GetVersionEx lies on Win 8.1+
        using RtlGetVersionFunc = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
        auto ntdll = GetModuleHandleW(L"ntdll.dll");
        if (ntdll) {
            auto rtlGetVersion = reinterpret_cast<RtlGetVersionFunc>(
                GetProcAddress(ntdll, "RtlGetVersion"));
            if (rtlGetVersion)
                rtlGetVersion(reinterpret_cast<PRTL_OSVERSIONINFOW>(&osvi));
        }

        juce::String osName = "Windows " + juce::String(osvi.dwMajorVersion)
            + "." + juce::String(osvi.dwMinorVersion)
            + " Build " + juce::String(osvi.dwBuildNumber);
        info("APP", "OS: " + osName);
    }

    // CPU info
    {
        SYSTEM_INFO si{};
        GetSystemInfo(&si);
        info("APP", "CPU cores: " + juce::String(si.dwNumberOfProcessors));
    }

    info("APP", "Process priority: HIGH_PRIORITY_CLASS");
    info("APP", "Timer resolution: 1ms (timeBeginPeriod)");
#else
    // Cross-platform OS info via JUCE
    info("APP", "OS: " + juce::SystemStats::getOperatingSystemName());
    info("APP", "CPU cores: " + juce::String(juce::SystemStats::getNumCpus()));
#endif

    info("APP", "─────────────────────────────────────────");
}

void audioConfig(const juce::String& driverType,
                 const juce::String& inputDevice,
                 const juce::String& outputDevice,
                 double sampleRate, int bufferSize)
{
    info("AUDIO", "Driver: " + driverType
        + " | Input: " + (inputDevice.isEmpty() ? "(none)" : inputDevice)
        + " | Output: " + (outputDevice.isEmpty() ? "(none)" : outputDevice)
        + " | SR: " + juce::String(static_cast<int>(sampleRate))
        + " | BS: " + juce::String(bufferSize));
}

void monitorConfig(const juce::String& deviceName, double sampleRate, int bufferSize)
{
    if (deviceName.isEmpty()) {
        info("MONITOR", "Device: (not configured)");
        return;
    }
    info("MONITOR", "Device: " + deviceName
        + " | SR: " + juce::String(static_cast<int>(sampleRate))
        + " | BS: " + juce::String(bufferSize));
}

void pluginChain(const juce::StringArray& pluginNames, int activeSlot, const juce::String& presetName)
{
    if (pluginNames.isEmpty()) {
        info("VST", "Chain: (empty)");
    } else {
        info("VST", "Chain: " + pluginNames.joinIntoString(", ")
            + " (" + juce::String(pluginNames.size()) + " plugins)");
    }

    juce::String slotStr;
    if (activeSlot == 5)
        slotStr = "Auto";
    else {
        char slotLabel = static_cast<char>('A' + juce::jlimit(0, 4, activeSlot));
        slotStr = juce::String::charToString(slotLabel);
    }
    info("PRESET", "Active slot: " + slotStr
        + (presetName.isNotEmpty() ? " | Preset: " + presetName : ""));
}

void sessionEnd(juce::int64 sessionStartTimeMs)
{
    auto elapsed = juce::Time::getMillisecondCounter() - sessionStartTimeMs;
    auto seconds = static_cast<int>(elapsed / 1000);
    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;

    juce::String duration;
    if (h > 0)
        duration = juce::String(h) + "h " + juce::String(m) + "m " + juce::String(s) + "s";
    else if (m > 0)
        duration = juce::String(m) + "m " + juce::String(s) + "s";
    else
        duration = juce::String(s) + "s";

    info("APP", "DirectPipe shutting down (session: " + duration + ")");
}

} // namespace Log
} // namespace directpipe
