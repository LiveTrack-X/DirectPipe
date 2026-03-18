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
#include "../../Util/AtomicFileIO.h"

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

/// Escape a path for the Exec key in a .desktop file (XDG spec).
/// Special characters (space, tab, $, `, \, ", &, |, ;, #, <, >, (, )) must be escaped with backslash.
static juce::String escapeDesktopExec(const juce::String& path)
{
    juce::String result;
    for (int i = 0; i < path.length(); ++i) {
        auto c = path[i];
        if (c == ' ' || c == '\t' || c == '$' || c == '`' || c == '\\' || c == '"'
            || c == '&' || c == '|' || c == ';' || c == '#'
            || c == '<' || c == '>' || c == '(' || c == ')')
            result += '\\';
        result += c;
    }
    return result;
}

bool setAutoStartEnabled(bool enable)
{
    auto desktopFile = getDesktopFile();

    if (enable) {
        if (!desktopFile.getParentDirectory().createDirectory()) {
            juce::Logger::writeToLog("[APP] Failed to create autostart directory");
            return false;
        }

        auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                           .getFullPathName();

        juce::String entry;
        entry << "[Desktop Entry]\n"
              << "Type=Application\n"
              << "Name=DirectPipe\n"
              << "Exec=" << escapeDesktopExec(exePath) << "\n"
              << "Terminal=false\n"
              << "X-GNOME-Autostart-enabled=true\n";

        if (!atomicWriteFile(desktopFile, entry)) {
            juce::Logger::writeToLog("[APP] Failed to write autostart desktop entry");
            return false;
        }
        return true;
    } else {
        bool ok = desktopFile.deleteFile();
        if (!ok)
            juce::Logger::writeToLog("[APP] Failed to delete autostart desktop entry");
        return ok;
    }
}

bool isAutoStartSupported() { return true; }

} // namespace Platform
} // namespace directpipe

#endif // defined(__linux__)
