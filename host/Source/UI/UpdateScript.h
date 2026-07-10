// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#pragma once

#include <JuceHeader.h>

#include <array>

namespace directpipe::update_detail {

struct WindowsUpdateInstallSpec {
    juce::String currentExePath;
    juce::String downloadedFilePath;
    juce::String stagedExePath;
    juce::String backupExePath;
    juce::String updateDirPath;
    bool isZip = false;
};

/** Strictly parse vMAJOR.MINOR.PATCH/MAJOR.MINOR.PATCH and return a safe canonical value. */
bool parseStrictReleaseVersion(const juce::String& value,
                               std::array<int, 3>& components,
                               juce::String& canonicalVersion);

/** Wait only for the DirectPipe process that launched the updater. */
juce::String buildWindowsUpdateWaitScript(unsigned long processId);

/** Builds the install-only portion of the Windows updater batch script. */
juce::String buildWindowsUpdateInstallScript(const WindowsUpdateInstallSpec& spec);

/** Builds the post-install flag and relaunch portion of the Windows updater script. */
juce::String buildWindowsUpdateCompletionScript(const juce::String& version,
                                                const juce::String& updatedFlagPath,
                                                const juce::String& currentExePath);

} // namespace directpipe::update_detail
