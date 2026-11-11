// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <functional>

namespace directpipe::update_detail {

/** A single canonical 32-lowercase-hex attempt token; no path comes from CLI. */
juce::String startupAttemptFromArguments(const juce::StringArray& arguments,
                                        juce::String& error);
juce::File startupAckFileForAttempt(const juce::File& receiptFile,
                                    const juce::String& attemptId);

/** Write only the ACK derived from a matching, pending, committed receipt.
    ready=false reports a settled but incomplete initial restore promptly. */
bool writeHostStartupAcknowledgement(const juce::File& receiptFile,
                                     const juce::String& attemptId,
                                     const juce::File& currentExe,
                                     const juce::String& runningVersion,
                                     unsigned long processId,
                                     bool ready,
                                     const juce::String& reason,
                                     juce::String& error);

struct HostExecutableIdentity {
    juce::String productName;
    juce::String fileVersion;
    juce::String productVersion;
    juce::String sha256;
};

using HostExecutableInspector = std::function<bool(
    const juce::File&, HostExecutableIdentity&, juce::String&)>;
bool inspectHostExecutableForCleanup(const juce::File& file,
                                     HostExecutableIdentity& identity,
                                     juce::String& error);

struct LegacyExecutableCleanupResult {
    bool attempted = false;
    bool complete = false;
    juce::StringArray removedFiles;
    juce::StringArray retainedFiles;
    juce::String warning;
};

/** Legacy flag-only finalization after initial restore/window policy settled.
    Any receipt or attempt token blocks this path. Only the two exact managed
    EXE names are eligible; current/manual/date-suffixed files and settings stay. */
LegacyExecutableCleanupResult cleanupLegacyUpdateExecutables(
    const juce::File& currentExe, const juce::String& runningVersion,
    const juce::String& updatedFlagVersion, const juce::File& receiptFile,
    const juce::String& attemptId, bool applicationReady,
    const HostExecutableInspector& inspector = inspectHostExecutableForCleanup);

} // namespace directpipe::update_detail
