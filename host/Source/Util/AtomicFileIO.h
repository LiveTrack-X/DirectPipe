// host/Source/Util/AtomicFileIO.h
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack
#pragma once

#include <JuceHeader.h>

namespace directpipe {

/**
 * Atomic file write: content → .tmp → rename original → .bak → rename .tmp → target.
 * On failure, original file is preserved. Returns true on success.
 */
[[nodiscard]] inline bool atomicWriteFile(const juce::File& targetFile, const juce::String& content)
{
    auto tmpFile = targetFile.getSiblingFile(targetFile.getFileName() + ".tmp");
    if (!tmpFile.replaceWithText(content))
        return false;

    auto bakFile = targetFile.getSiblingFile(targetFile.getFileName() + ".bak");
    if (targetFile.existsAsFile()) {
        if (!targetFile.moveFileTo(bakFile)) {
            tmpFile.deleteFile();
            return false;
        }
    }

    if (!tmpFile.moveFileTo(targetFile)) {
        if (bakFile.existsAsFile())
            bakFile.moveFileTo(targetFile);
        tmpFile.deleteFile();
        return false;
    }

    return true;
}

/**
 * Load file content with automatic .bak fallback on corruption/missing.
 * Returns empty string if no valid file found.
 */
inline juce::String loadFileWithBackupFallback(const juce::File& file)
{
    // Try main file
    if (file.existsAsFile()) {
        auto content = file.loadFileAsString();
        if (juce::JSON::parse(content).isObject())
            return content;
        juce::Logger::writeToLog("[IO] JSON parse failed: " + file.getFileName()
            + " — first 200 chars: " + content.substring(0, 200));
    }

    // Try .bak
    auto bakFile = file.getSiblingFile(file.getFileName() + ".bak");
    if (bakFile.existsAsFile()) {
        auto content = bakFile.loadFileAsString();
        if (juce::JSON::parse(content).isObject()) {
            juce::Logger::writeToLog("[IO] Restored from backup: " + bakFile.getFileName());
            bakFile.copyFileTo(file);
            return content;
        }
    }

    // Try legacy .backup extension (v3 compat)
    auto legacyBak = file.withFileExtension(file.getFileExtension() + ".backup");
    if (legacyBak.existsAsFile()) {
        auto content = legacyBak.loadFileAsString();
        if (juce::JSON::parse(content).isObject()) {
            juce::Logger::writeToLog("[IO] Restored from legacy backup: " + legacyBak.getFileName());
            legacyBak.copyFileTo(file);
            return content;
        }
    }

    return {};
}

} // namespace directpipe
