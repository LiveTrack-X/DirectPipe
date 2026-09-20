// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#pragma once

#include <JuceHeader.h>

#include <array>
#include "ReceiverUpdateSupport.h"

namespace directpipe::update_detail {

struct WindowsUpdateInstallSpec {
    juce::String currentExePath;
    juce::String downloadedFilePath;
    juce::String stagedExePath;
    juce::String backupExePath;
    juce::String updateDirPath;
    juce::String expectedVersion;
    bool isZip = false;
    std::vector<ReceiverInstallTarget> receiverTargets;
    bool skipHostUpdate = false;
    unsigned long processId = 0;
    juce::String resultFilePath;
    juce::String updatedFlagPath;
    bool relaunchHostAfterUpdate = false;
    juce::String expectedPackageSha256;
};

/** Strictly parse vMAJOR.MINOR.PATCH/MAJOR.MINOR.PATCH and return a safe canonical value. */
bool parseStrictReleaseVersion(const juce::String& value,
                               std::array<int, 3>& components,
                               juce::String& canonicalVersion);

/** Wait only for the DirectPipe process that launched the updater. */
juce::String buildWindowsUpdateWaitScript(unsigned long processId);

/**
 * Builds the legacy host-only Windows batch install path (no Receiver targets).
 * Returns an empty string when expectedVersion is not strict MAJOR.MINOR.PATCH.
 */
juce::String buildWindowsUpdateInstallScript(const WindowsUpdateInstallSpec& spec);

/** Standalone Windows PowerShell transaction for verified, strictly older Receiver
    targets and an optional host. Requires a hashed ZIP containing the host plus
    complete VST2/VST3 payloads, even for Receiver-only maintenance. Revalidates
    identities/hashes/file use, stages beside each destination and retains backups.
    Caller launches the UTF-8 .ps1 directly (runas if either destination needs it).
    Receiver-only skips host exit/replacement/relaunch; paired work waits for the
    launching host PID. It never terminates OBS or other audio applications.
    VST3 directory renames release/reacquire that bundle's handles and recheck its
    contents. Failure attempts reverse-order rollback; result.json distinguishes
    rollback failure/manual recovery. This is not an atomic multi-file installer.
    pathPreflightOnly validates all generated paths using private archive extraction,
    without waiting for the host, writing installation files, or relaunching. The
    caller runs that mode before launching the installer/closing the host, with a
    separate result path. The installer repeats the checks before replacement.
    Empty means the plan is invalid; file success does not prove host reload/audio. */
juce::String buildWindowsCompanionInstallPowerShell(const WindowsUpdateInstallSpec& spec,
                                                    bool pathPreflightOnly = false);

/** Builds the post-install flag and relaunch portion of the Windows updater script. */
juce::String buildWindowsUpdateCompletionScript(const juce::String& version,
                                                const juce::String& updatedFlagPath,
                                                const juce::String& currentExePath);

} // namespace directpipe::update_detail
