// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace directpipe::update_detail {

enum class ReceiverFormat { vst2, vst3 };

struct ReceiverInstallTarget {
    ReceiverFormat format = ReceiverFormat::vst2;
    juce::String installPath; // DLL, or complete .vst3 bundle directory
    juce::String binaryPath;  // exact PE module, never a folder
    juce::String installedVersion;
    bool needsElevation = false;
};

struct ReceiverDiscoveryResult {
    std::vector<ReceiverInstallTarget> targets;
    juce::StringArray warnings;
};

struct ReceiverUseResult {
    bool inspectable = false;
    bool busy = false;
    juce::StringArray processes;
    juce::String error;
};

/** Standard Windows VST2/VST3 roots, per-user VST3 and HKCU/HKLM VST2 paths
    from both registry views. Does not search arbitrary disks or OBS settings. */
std::vector<juce::File> receiverSearchDirectories();

/** Read exact-name Windows x64 DLL/bundle metadata and format exports without
    executing it. Requires DirectPipe Receiver identity, matching strict file/product
    versions and no reparse ancestors; this is not a publisher-signature check. */
bool inspectReceiverTarget(const juce::File& installPath,
                           ReceiverInstallTarget& target, juce::String& error);

/** Search standard roots plus an optional custom directory, not a new destination.
    Traversal is bounded (depth 6 / 20,000 visits); excludes build/dist/backup trees
    and reports inaccessible, invalid or incomplete searches as warnings. */
ReceiverDiscoveryResult discoverInstalledReceivers(const juce::File& customFolder = {});

/** Deterministic directory/inspection seam: tests inject temp roots and an inspector;
    production uses inspectReceiverTarget. Discovery never connects to audio IPC. */
using ReceiverTargetInspector = std::function<bool(const juce::File&,
                                                   ReceiverInstallTarget&, juce::String&)>;
ReceiverDiscoveryResult discoverReceiversInDirectories(
    const std::vector<juce::File>& directories,
    const ReceiverTargetInspector& inspector = inspectReceiverTarget);

/** Strict versions; same/newer installs must not be replaced or downgraded. */
bool receiverNeedsUpdate(const juce::String& installedVersion,
                         const juce::String& releaseVersion);

/** Revalidate previewed identity, query Restart Manager for exact binary paths and
    probe exclusive read access. No process is stopped and no lock is retained on
    return; the installer must recheck complete bundles before replacing files. */
ReceiverUseResult queryReceiverUse(const std::vector<ReceiverInstallTarget>& targets);

} // namespace directpipe::update_detail
