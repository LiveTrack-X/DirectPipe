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

void setAutoStartEnabled(bool enable)
{
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return;

    if (enable) {
        auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                           .getFullPathName();
        auto wPath = exePath.toWideCharPointer();
        RegSetValueExW(hKey, getRunValueName(), 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(wPath),
                       static_cast<DWORD>((wcslen(wPath) + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(hKey, getRunValueName());
    }
    RegCloseKey(hKey);
}

bool isAutoStartSupported() { return true; }

} // namespace Platform
} // namespace directpipe

#endif // defined(_WIN32)
