// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file MacAutoStart.mm
 * @brief macOS LaunchAgent-based auto-start implementation
 *
 * Creates/removes ~/Library/LaunchAgents/com.directpipe.host.plist
 * to control whether DirectPipe launches at login.
 */

#include "../AutoStart.h"

#if defined(__APPLE__)

#include <JuceHeader.h>

namespace directpipe {
namespace Platform {

static juce::File getLaunchAgentFile()
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/LaunchAgents/com.directpipe.host.plist");
}

bool isAutoStartEnabled()
{
    return getLaunchAgentFile().existsAsFile();
}

void setAutoStartEnabled(bool enable)
{
    auto plistFile = getLaunchAgentFile();

    if (enable) {
        // Ensure LaunchAgents directory exists
        plistFile.getParentDirectory().createDirectory();

        auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                           .getFullPathName();

        // Write a minimal launchd plist
        juce::String plist;
        plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\"\n"
              << "  \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
              << "<plist version=\"1.0\">\n"
              << "<dict>\n"
              << "  <key>Label</key>\n"
              << "  <string>com.directpipe.host</string>\n"
              << "  <key>ProgramArguments</key>\n"
              << "  <array>\n"
              << "    <string>" << exePath << "</string>\n"
              << "  </array>\n"
              << "  <key>RunAtLoad</key>\n"
              << "  <true/>\n"
              << "</dict>\n"
              << "</plist>\n";

        plistFile.replaceWithText(plist);
    } else {
        plistFile.deleteFile();
    }
}

bool isAutoStartSupported() { return true; }

} // namespace Platform
} // namespace directpipe

#endif // defined(__APPLE__)
