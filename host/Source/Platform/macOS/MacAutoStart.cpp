// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

/**
 * @file MacAutoStart.cpp
 * @brief macOS LaunchAgent-based auto-start implementation
 *
 * Creates/removes ~/Library/LaunchAgents/com.directpipe.host.plist
 * to control whether DirectPipe launches at login.
 */

#include "../AutoStart.h"

#if defined(__APPLE__)

#include <JuceHeader.h>
#include "../../Util/AtomicFileIO.h"

namespace directpipe {
namespace Platform {

static juce::File getLaunchAgentFile()
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library/LaunchAgents/com.directpipe.host.plist");
}

/// Escape XML special characters in a string for safe embedding in plist XML.
static juce::String escapeXml(const juce::String& text)
{
    return text.replace("&", "&amp;")
               .replace("<", "&lt;")
               .replace(">", "&gt;")
               .replace("\"", "&quot;")
               .replace("'", "&apos;");
}

bool isAutoStartEnabled()
{
    return getLaunchAgentFile().existsAsFile();
}

bool setAutoStartEnabled(bool enable)
{
    auto plistFile = getLaunchAgentFile();

    if (enable) {
        // Ensure LaunchAgents directory exists
        if (!plistFile.getParentDirectory().createDirectory()) {
            juce::Logger::writeToLog("[APP] Failed to create LaunchAgents directory");
            return false;
        }

        auto exePath = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
                           .getFullPathName();

        // Write a minimal launchd plist (escape path for XML safety)
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
              << "    <string>" << escapeXml(exePath) << "</string>\n"
              << "  </array>\n"
              << "  <key>RunAtLoad</key>\n"
              << "  <true/>\n"
              << "</dict>\n"
              << "</plist>\n";

        if (!atomicWriteFile(plistFile, plist)) {
            juce::Logger::writeToLog("[APP] Failed to write LaunchAgent plist");
            return false;
        }
        return true;
    } else {
        bool ok = plistFile.deleteFile();
        if (!ok)
            juce::Logger::writeToLog("[APP] Failed to delete LaunchAgent plist");
        return ok;
    }
}

bool isAutoStartSupported() { return true; }

} // namespace Platform
} // namespace directpipe

#endif // defined(__APPLE__)
