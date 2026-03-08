// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file LinuxAutoStart.cpp
 * @brief Linux XDG autostart implementation
 *
 * Creates/removes ~/.config/autostart/directpipe.desktop
 * following the XDG Desktop Entry specification.
 */

#include "../AutoStart.h"

#if defined(__linux__)

#include <JuceHeader.h>

namespace directpipe {
namespace Platform {

static juce::File getDesktopFile()
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".config/autostart/directpipe.desktop");
}

bool isAutoStartEnabled()
{
    return getDesktopFile().existsAsFile();
}

void setAutoStartEnabled(bool enable)
{
    auto desktopFile = getDesktopFile();

    if (enable) {
        desktopFile.getParentDirectory().createDirectory();

        auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                           .getFullPathName();

        juce::String entry;
        entry << "[Desktop Entry]\n"
              << "Type=Application\n"
              << "Name=DirectPipe\n"
              << "Exec=" << exePath << "\n"
              << "Terminal=false\n"
              << "X-GNOME-Autostart-enabled=true\n";

        desktopFile.replaceWithText(entry);
    } else {
        desktopFile.deleteFile();
    }
}

bool isAutoStartSupported() { return true; }

} // namespace Platform
} // namespace directpipe

#endif // defined(__linux__)
