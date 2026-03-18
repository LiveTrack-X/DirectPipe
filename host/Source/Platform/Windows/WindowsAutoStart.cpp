// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file WindowsAutoStart.cpp
 * @brief Windows Registry-based auto-start implementation
 */

#include "../AutoStart.h"

#if defined(_WIN32)

#include <JuceHeader.h>
#include <Windows.h>
#include "../../Control/ControlMapping.h"

namespace directpipe {
namespace Platform {

static const wchar_t* kRunKeyPath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kRunValueName = L"DirectPipe";
static const wchar_t* kRunValueNamePortable = L"DirectPipe (Portable)";

static const wchar_t* getRunValueName()
{
    return ControlMappingStore::isPortableMode() ? kRunValueNamePortable : kRunValueName;
}

bool isAutoStartEnabled()
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    DWORD type = 0;
    DWORD size = 0;
    bool exists = (RegQueryValueExW(hKey, getRunValueName(), nullptr, &type, nullptr, &size) == ERROR_SUCCESS);
    RegCloseKey(hKey);
    return exists;
}

bool setAutoStartEnabled(bool enable)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    if (enable) {
        auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                           .getFullPathName();
        auto wPath = exePath.toWideCharPointer();
        auto result = RegSetValueExW(hKey, getRunValueName(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(wPath),
                       static_cast<DWORD>((wcslen(wPath) + 1) * sizeof(wchar_t)));
        RegCloseKey(hKey);
        if (result != ERROR_SUCCESS) {
            juce::Logger::writeToLog("[APP] Failed to write auto-start registry (error " + juce::String((int)result) + ")");
            return false;
        }
        return true;
    } else {
        auto result = RegDeleteValueW(hKey, getRunValueName());
        RegCloseKey(hKey);
        if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND) {
            juce::Logger::writeToLog("[APP] Failed to delete auto-start registry (error " + juce::String((int)result) + ")");
            return false;
        }
        return true;
    }
}

bool isAutoStartSupported() { return true; }

} // namespace Platform
} // namespace directpipe

#endif // defined(_WIN32)
