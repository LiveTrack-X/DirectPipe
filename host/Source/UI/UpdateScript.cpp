// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2025 LiveTrack

#include "UpdateScript.h"

#include <limits>

namespace directpipe::update_detail {

namespace {

juce::String escapePowerShellQuote(const juce::String& value)
{
    return value.replace("'", "''");
}

juce::String escapeBatchPercent(const juce::String& value)
{
    // cmd.exe expands %NAME% even inside quoted arguments. Doubling each
    // percent preserves literal portable-install paths in the generated BAT.
    return value.replace("%", "%%");
}

} // namespace

bool parseStrictReleaseVersion(const juce::String& value,
                               std::array<int, 3>& components,
                               juce::String& canonicalVersion)
{
    auto version = value.trim();
    if (version.startsWithChar('v') || version.startsWithChar('V'))
        version = version.substring(1);

    if (version.isEmpty() || !version.containsOnly("0123456789."))
        return false;

    juce::StringArray parts;
    parts.addTokens(version, ".", "");
    const int dotCount = version.length() - version.removeCharacters(".").length();
    if (parts.size() != 3 || dotCount != 2)
        return false;

    std::array<int, 3> parsed{};
    for (int i = 0; i < 3; ++i) {
        if (parts[i].isEmpty() || !parts[i].containsOnly("0123456789")
            || parts[i].length() > 10) {
            return false;
        }
        const auto number = parts[i].getLargeIntValue();
        if (number < 0 || number > (std::numeric_limits<int>::max)())
            return false;
        parsed[static_cast<size_t>(i)] = static_cast<int>(number);
    }

    components = parsed;
    canonicalVersion = juce::String(parsed[0]) + "."
        + juce::String(parsed[1]) + "." + juce::String(parsed[2]);
    return true;
}

juce::String buildWindowsUpdateWaitScript(unsigned long processId)
{
    if (processId == 0)
        return {};

    juce::String script;
    script << "echo  Waiting for this DirectPipe instance to close...\r\n";
    script << "powershell -NoProfile -Command \"while (Get-Process -Id "
           << juce::String(processId)
           << " -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 250 }\"\r\n";
    script << "if errorlevel 1 exit /b 3\r\n";
    return script;
}

juce::String buildWindowsUpdateInstallScript(const WindowsUpdateInstallSpec& spec)
{
    std::array<int, 3> expectedVersionComponents{};
    juce::String canonicalExpectedVersion;
    if (!parseStrictReleaseVersion(spec.expectedVersion,
                                   expectedVersionComponents,
                                   canonicalExpectedVersion)) {
        return {};
    }

    const WindowsUpdateInstallSpec paths {
        escapeBatchPercent(spec.currentExePath),
        escapeBatchPercent(spec.downloadedFilePath),
        escapeBatchPercent(spec.stagedExePath),
        escapeBatchPercent(spec.backupExePath),
        escapeBatchPercent(spec.updateDirPath),
        canonicalExpectedVersion,
        spec.isZip,
    };

    juce::String script;
    juce::String installSourcePath = paths.downloadedFilePath;

    // A missing installed executable cannot be repaired by validating or
    // extracting the candidate. Fail before PowerShell startup or backup
    // rotation so an existing known-good backup remains untouched.
    script << "if not exist \"" << paths.currentExePath
           << "\" goto update_install_failed\r\n";

    if (paths.isZip) {
        script << "echo  Extracting update...\r\n";
        script << "if exist \"" << paths.updateDirPath << "\" rd /s /q \""
               << paths.updateDirPath << "\"\r\n";
        script << "if exist \"" << paths.updateDirPath
               << "\" goto update_install_failed\r\n";
        script << "powershell -NoProfile -Command \"$ErrorActionPreference = 'Stop'; "
               << "Expand-Archive -LiteralPath '"
               << escapePowerShellQuote(paths.downloadedFilePath)
               << "' -DestinationPath '" << escapePowerShellQuote(paths.updateDirPath)
               << "' -Force\"\r\n";
        script << "if errorlevel 1 goto update_install_failed\r\n";
        script << "if exist \"" << paths.stagedExePath << "\" del /f /q \""
               << paths.stagedExePath << "\"\r\n";
        script << "if exist \"" << paths.stagedExePath
               << "\" goto update_install_failed\r\n";
        script << "powershell -NoProfile -Command \"$ErrorActionPreference = 'Stop'; "
               << "$candidates = @(Get-ChildItem -LiteralPath '"
               << escapePowerShellQuote(paths.updateDirPath)
               << "' -Recurse -File -Filter 'DirectPipe.exe'); "
               << "if ($candidates.Count -ne 1) { throw ('Expected exactly one "
                  "DirectPipe.exe in update archive; found ' + $candidates.Count) }; "
               << "Copy-Item -LiteralPath $candidates[0].FullName -Destination '"
               << escapePowerShellQuote(paths.stagedExePath) << "' -Force\"\r\n";
        script << "if errorlevel 1 goto update_install_failed\r\n";
        script << "if not exist \"" << paths.stagedExePath
               << "\" goto update_install_failed\r\n";
        installSourcePath = paths.stagedExePath;
    } else {
        script << "echo  Applying update...\r\n";
        script << "if not exist \"" << installSourcePath
               << "\" goto update_install_failed\r\n";
    }

    // The archive checksum authenticates the package bytes. Independently bind
    // the executable selected from that package to the release version before
    // rotating the known-good binary.
    script << "powershell -NoProfile -Command \"$ErrorActionPreference = 'Stop'; "
           << "$f = Get-Item -LiteralPath '"
           << escapePowerShellQuote(installSourcePath) << "'; "
           << "$v = $f.VersionInfo; "
           << "if (($v.FileVersion -ne '" << paths.expectedVersion
           << "') -or ($v.ProductVersion -ne '" << paths.expectedVersion
           << "')) { throw ('Update executable version mismatch: FileVersion=' "
              "+ $v.FileVersion + ', ProductVersion=' + $v.ProductVersion "
              "+ ', expected=" << paths.expectedVersion << "') }\"\r\n";
    script << "if errorlevel 1 goto update_install_failed\r\n";

    // Rotate the known-good executable only after a replacement has been staged.
    script << "if exist \"" << paths.backupExePath << "\" del /f /q \""
           << paths.backupExePath << "\"\r\n";
    script << "if exist \"" << paths.backupExePath
           << "\" goto update_install_failed\r\n";
    script << "move /y \"" << paths.currentExePath << "\" \""
           << paths.backupExePath << "\"\r\n";
    script << "if errorlevel 1 goto update_install_failed\r\n";
    script << "move /y \"" << installSourcePath << "\" \""
           << paths.currentExePath << "\"\r\n";
    script << "if errorlevel 1 goto update_install_rollback\r\n";
    script << "if not exist \"" << paths.currentExePath
           << "\" goto update_install_rollback\r\n";

    if (paths.isZip) {
        script << "rd /s /q \"" << paths.updateDirPath << "\"\r\n";
        script << "del /f /q \"" << paths.downloadedFilePath << "\"\r\n";
    }

    // Keep the backup through subsequent startups. The next update rotation
    // replaces it only after another candidate has passed identity validation.
    script << "goto update_install_complete\r\n";
    script << ":update_install_rollback\r\n";
    script << "if exist \"" << paths.currentExePath << "\" del /f /q \""
           << paths.currentExePath << "\"\r\n";
    script << "if exist \"" << paths.backupExePath << "\" move /y \""
           << paths.backupExePath << "\" \"" << paths.currentExePath << "\"\r\n";
    script << "if not exist \"" << paths.currentExePath << "\" exit /b 2\r\n";
    script << ":update_install_failed\r\n";
    script << "echo  Update failed; original DirectPipe executable was preserved.\r\n";
    script << "exit /b 1\r\n";
    script << ":update_install_complete\r\n";

    return script;
}

juce::String buildWindowsUpdateCompletionScript(const juce::String& version,
                                                const juce::String& updatedFlagPath,
                                                const juce::String& currentExePath)
{
    std::array<int, 3> components{};
    juce::String canonicalVersion;
    if (!parseStrictReleaseVersion(version, components, canonicalVersion))
        return {};

    const auto escapedFlagPath = escapeBatchPercent(updatedFlagPath);
    const auto escapedCurrentPath = escapeBatchPercent(currentExePath);

    juce::String script;
    script << "echo " << canonicalVersion << " > \"" << escapedFlagPath << "\"\r\n";
    script << "powershell -NoProfile -Command \"Start-Process -FilePath '"
           << escapePowerShellQuote(escapedCurrentPath) << "'\"\r\n";
    return script;
}

} // namespace directpipe::update_detail
