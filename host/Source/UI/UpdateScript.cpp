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

juce::String buildWindowsCompanionInstallPowerShell(const WindowsUpdateInstallSpec& spec,
                                                    bool pathPreflightOnly)
{
    // This generates an external transaction, not a plug-in loader. Source and
    // destination checks run again in that process after any UAC/user delay.
    // Whole VST3 folders require a handle-release/rename/reacquire interval on
    // Windows; the checks and retained backups detect/recover failures, not a
    // guarantee of atomic replacement against arbitrary concurrent installers.
    std::array<int, 3> parts{};
    juce::String version;
    if (!spec.isZip || spec.receiverTargets.empty()
        || !parseStrictReleaseVersion(spec.expectedVersion, parts, version)
        || spec.expectedPackageSha256.length() != 64
        || !spec.expectedPackageSha256.containsOnly("0123456789abcdefABCDEF")) return {};
    auto validPath = [](const juce::String& path) {
        return path.isNotEmpty() && juce::File::isAbsolutePath(path)
            && !path.containsChar('\0') && !path.containsAnyOf("\r\n");
    };
    if (!validPath(spec.downloadedFilePath) || !validPath(spec.resultFilePath)
        || (!spec.skipHostUpdate && (!validPath(spec.currentExePath)
            || !validPath(spec.backupExePath) || !validPath(spec.updatedFlagPath)))) return {};

    auto plan = std::make_unique<juce::DynamicObject>();
    plan->setProperty("currentExePath", spec.currentExePath);
    plan->setProperty("downloadedFilePath", spec.downloadedFilePath);
    plan->setProperty("backupExePath", spec.backupExePath);
    plan->setProperty("expectedVersion", version);
    plan->setProperty("skipHostUpdate", spec.skipHostUpdate);
    plan->setProperty("processId", static_cast<juce::int64>(spec.processId));
    plan->setProperty("resultFilePath", spec.resultFilePath);
    plan->setProperty("updatedFlagPath", spec.updatedFlagPath);
    plan->setProperty("relaunchHostAfterUpdate", spec.relaunchHostAfterUpdate);
    plan->setProperty("expectedPackageSha256", spec.expectedPackageSha256.toLowerCase());
    plan->setProperty("pathPreflightOnly", pathPreflightOnly);
    juce::Array<juce::var> targets;
    juce::StringArray distinct;
    for (const auto& target : spec.receiverTargets) {
        if (!validPath(target.installPath) || !validPath(target.binaryPath)
            || !receiverNeedsUpdate(target.installedVersion, version)) return {};
        const juce::File install(target.installPath);
        const bool vst2 = target.format == ReceiverFormat::vst2;
        const auto expectedName = vst2 ? "DirectPipe Receiver.dll" : "DirectPipe Receiver.vst3";
        const auto binary = vst2 ? install : install.getChildFile("Contents/x86_64-win/DirectPipe Receiver.vst3");
        if (!install.getFileName().equalsIgnoreCase(expectedName)
            || binary != juce::File(target.binaryPath)
            || distinct.contains(install.getFullPathName(), true)) return {};
        distinct.add(install.getFullPathName());
        auto object = std::make_unique<juce::DynamicObject>();
        object->setProperty("format", vst2 ? "vst2" : "vst3");
        object->setProperty("path", install.getFullPathName());
        object->setProperty("binary", binary.getFullPathName());
        object->setProperty("version", target.installedVersion);
        targets.add(juce::var(object.release()));
    }
    plan->setProperty("targets", targets);
    const auto json = juce::JSON::toString(juce::var(plan.release()), true);
    juce::String script;
    // Base64 is only an encoding, not trust. It keeps every literal path out
    // of shell syntax, including apostrophes, %, &, $, and Unicode. The plan
    // validation above rejects NUL/newline paths before they reach this encoder.
    script << "$ErrorActionPreference = 'Stop'\nSet-StrictMode -Version 2\n"
           << "$plan = [Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('"
           << juce::Base64::toBase64(json.toRawUTF8(), json.getNumBytesAsUTF8())
           << "')) | ConvertFrom-Json\n";
    script << R"DIRECTPIPE(
$entries = New-Object 'System.Collections.Generic.List[object]'
$locks = @()
$archiveLock = $null
$archiveHashes = @{}
$work = $null
$result = @{ success = $false; error = ''; rollbackSucceeded = $true; installed = @(); hostUpdated = $false; relaunchError = '' }

function Assert-SupportedPathLength([string]$path) {
    # Restart Manager rejects paths of 260 UTF-16 units even when .NET can open
    # them. Check every generated name, including complete bundle descendants.
    if ([IO.Path]::GetFullPath($path).Length -ge 260) {
        throw "Update path is too long for Windows file-use inspection (maximum 259 characters). Use a shorter installation folder: $path"
    }
}

function Assert-LiteralPath([string]$path, [bool]$mustExist = $true) {
    if (-not [IO.Path]::IsPathRooted($path) -or $path.StartsWith('\\') -or $path.Length -lt 3 -or $path.Substring(2).Contains(':')) { throw "Unsupported update path: $path" }
    $full = [IO.Path]::GetFullPath($path)
    Assert-SupportedPathLength $full
    if ($mustExist -and -not (Test-Path -LiteralPath $full)) { throw "Update target is missing: $full" }
    $cursor = $full
    while ($cursor) {
        if (Test-Path -LiteralPath $cursor) {
            $item = Get-Item -LiteralPath $cursor -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Linked update paths are not supported: $cursor" }
        }
        $parent = [IO.Path]::GetDirectoryName($cursor.TrimEnd('\'))
        if ($parent -eq $cursor) { break }
        $cursor = $parent
    }
    return $full
}

function Get-ReleaseVersion([string]$text) {
    if ($text -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') { throw "Unrecognized release version: $text" }
    return [Version]$text
}

function Get-FileDigest([string]$path) {
    # Do not depend on Get-FileHash/module discovery: Windows PowerShell may
    # inherit a PSModulePath from a newer shell that cannot load that cmdlet.
    $stream = [IO.File]::OpenRead($path)
    $hasher = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
    finally { $hasher.Dispose(); $stream.Dispose() }
}

function Get-PEExport([string]$path, [string]$wanted) {
    $stream = [IO.File]::OpenRead($path)
    try {
        $reader = New-Object IO.BinaryReader($stream)
        if ($stream.Length -lt 256 -or $reader.ReadUInt16() -ne 0x5a4d) { throw 'Missing PE header' }
        $stream.Position = 0x3c; $pe = $reader.ReadInt32()
        if ($pe -lt 0 -or $pe -gt $stream.Length - 264) { throw 'Invalid PE offset' }
        $stream.Position = $pe
        if ($reader.ReadUInt32() -ne 0x4550 -or $reader.ReadUInt16() -ne 0x8664) { throw 'Expected Windows x64 binary' }
        $sections = $reader.ReadUInt16(); $stream.Position = $pe + 20; $optionalSize = $reader.ReadUInt16()
        $stream.Position = $pe + 24
        if ($reader.ReadUInt16() -ne 0x20b -or $sections -gt 96) { throw 'Invalid x64 PE layout' }
        if (-not $wanted) { return }
        $stream.Position = $pe + 24 + 112; $exportRva = $reader.ReadUInt32()
        $map = @()
        for ($i = 0; $i -lt $sections; ++$i) {
            $stream.Position = $pe + 24 + $optionalSize + $i * 40 + 12
            $map += @{ rva = $reader.ReadUInt32(); size = $reader.ReadUInt32(); raw = $reader.ReadUInt32() }
        }
        function Resolve-Rva([uint32]$rva) {
            foreach ($section in $map) {
                if ($rva -ge $section.rva -and ([long]$rva - $section.rva) -lt $section.size) {
                    $offset = [long]$section.raw + $rva - $section.rva
                    if ($offset -ge $stream.Length) { throw 'Invalid PE section offset' }
                    return $offset
                }
            }
            throw 'Invalid PE export address'
        }
        $stream.Position = (Resolve-Rva $exportRva) + 24; $count = $reader.ReadUInt32()
        if ($count -gt 65536) { throw 'Invalid PE export count' }
        $stream.Position += 4; $names = Resolve-Rva ($reader.ReadUInt32())
        for ($i = 0; $i -lt $count; ++$i) {
            $stream.Position = $names + $i * 4; $nameOffset = Resolve-Rva ($reader.ReadUInt32())
            $stream.Position = $nameOffset; $name = ''
            for ($j = 0; $j -lt 256; ++$j) { $char = $reader.ReadByte(); if ($char -eq 0) { break }; $name += [char]$char }
            if ($name -ceq $wanted) { return }
        }
        throw "Missing plug-in export: $wanted"
    } finally { $stream.Dispose() }
}

function Assert-Binary([string]$path, [string]$product, [string]$version, [string]$export) {
    $null = Assert-LiteralPath $path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Expected binary file: $path" }
    $identity = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
    if ($identity.ProductName -cne $product -or $identity.FileVersion -cne $version -or $identity.ProductVersion -cne $version) {
        throw "Product/version identity mismatch: $path"
    }
    $null = Get-ReleaseVersion $version
    Get-PEExport $path $export
}

function Get-TreeFiles([string]$path, [bool]$includeDirectories = $false) {
    $path = Assert-LiteralPath $path
    if (Test-Path -LiteralPath $path -PathType Leaf) { return @($path) }
    $files = @()
    $pending = New-Object 'System.Collections.Generic.Queue[string]'; $pending.Enqueue($path)
    $count = 0
    while ($pending.Count -gt 0) {
        foreach ($item in @(Get-ChildItem -LiteralPath $pending.Dequeue() -Force)) {
            if (++$count -gt 4096) { throw 'Receiver bundle is too large to inspect safely' }
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Linked bundle item: $($item.FullName)" }
            if ($item.PSIsContainer) {
                $pending.Enqueue($item.FullName)
                if ($includeDirectories) { $files += $item.FullName }
            } else { $files += $item.FullName }
        }
    }
    return $files
}

function Assert-MappedTreePaths([string]$source, [string[]]$destinations) {
    foreach ($destination in $destinations) { Assert-SupportedPathLength $destination }
    foreach ($path in @(Get-TreeFiles $source $true)) {
        $relative = $path.Substring($source.Length).TrimStart('\')
        foreach ($destination in $destinations) {
            $mapped = if ($relative) { Join-Path $destination $relative } else { $destination }
            Assert-SupportedPathLength $mapped
        }
    }
}

function Assert-EntryPaths($entry) {
    # Sources move to target/stage; current files move to backup; an existing
    # backup rotates to priorBackup. Include resources and empty directories.
    Assert-MappedTreePaths $entry.source @($entry.target, $entry.stage)
    Assert-MappedTreePaths $entry.target @($entry.target, $entry.backup)
    Assert-SupportedPathLength $entry.priorBackup
    if (Test-Path -LiteralPath $entry.backup) {
        Assert-MappedTreePaths $entry.backup @($entry.backup, $entry.priorBackup)
    }
}

function Remove-OwnedStage([string]$path) {
    if (Test-Path -LiteralPath $path) {
        $null = Get-TreeFiles $path
        Remove-Item -LiteralPath $path -Recurse -Force
    }
}

function Move-Exact([string]$source, [string]$destination) {
    $null = Assert-LiteralPath $source
    $null = Assert-LiteralPath $destination $false
    # Directory.Move fails on an existing destination; unlike Move-Item it
    # can never silently nest a VST3 bundle inside a concurrently created folder.
    if (Test-Path -LiteralPath $source -PathType Container) {
        [IO.Directory]::Move($source, $destination)
    } else {
        [IO.File]::Move($source, $destination)
    }
}

function Assert-LockedHashes([IO.FileStream[]]$held, [hashtable]$expectedHashes) {
    foreach ($file in $held) {
        $hasher = [Security.Cryptography.SHA256]::Create()
        try {
            $actual = [BitConverter]::ToString($hasher.ComputeHash($file)).Replace('-', '')
            $file.Position = 0
            if (-not $expectedHashes.ContainsKey($file.Name) -or $actual -ine $expectedHashes[$file.Name]) {
                throw "File changed between verification and exclusive access: $($file.Name)"
            }
        } finally { $hasher.Dispose() }
    }
}

function Expand-VerifiedArchive([string]$archive, [string]$destination) {
    # Framework GetFullPath expands existing 8.3 aliases (e.g. RUNNER~1).
    # Canonicalize the root too, so the strict separator-boundary comparison
    # uses the same representation as each extracted entry.
    $destination = Assert-LiteralPath $destination
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($archive)
    try {
        if ($zip.Entries.Count -gt 4096) { throw 'Update archive has too many entries' }
        $seen = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
        [long]$total = 0
        foreach ($entry in $zip.Entries) {
            $relative = $entry.FullName.Replace('/', '\')
            if (-not $relative -or [IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') -or ($relative.Split('\') -contains '..')) { throw 'Unsafe update archive entry' }
            if ((($entry.ExternalAttributes -shr 16) -band 0xf000) -eq 0xa000) { throw 'Linked archive entries are not supported' }
            $path = [IO.Path]::GetFullPath((Join-Path $destination $relative))
            Assert-SupportedPathLength $path
            if (-not $path.StartsWith($destination.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "Escaping archive entry: $relative" }
            if (-not $seen.Add($path)) { throw "Duplicate archive entry: $relative" }
            $total += $entry.Length
            if ($total -gt 536870912) { throw 'Update archive is too large' }
            if ($relative.EndsWith('\')) { $null = [IO.Directory]::CreateDirectory($path); continue }
            $null = [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($path))
            $input = $entry.Open()
            $output = [IO.File]::Open($path, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
            $hash = [Security.Cryptography.SHA256]::Create()
            try {
                $buffer = New-Object byte[] 65536
                while (($read = $input.Read($buffer, 0, $buffer.Length)) -gt 0) {
                    $null = $hash.TransformBlock($buffer, 0, $read, $buffer, 0)
                    $output.Write($buffer, 0, $read)
                }
                $null = $hash.TransformFinalBlock([byte[]]@(), 0, 0)
                $archiveHashes[$path] = [BitConverter]::ToString($hash.Hash).Replace('-', '').ToLowerInvariant()
            } finally { $hash.Dispose(); $output.Dispose(); $input.Dispose() }
        }
    } finally { $zip.Dispose() }
}

function New-Entry([string]$source, [string]$target, [string]$backup, [string]$format, [string]$priorVersion) {
    # FileInfo/FileStream names and dictionary keys must share this form during
    # staging, bundle-relative path mapping, identity checks, and rollback.
    $source = Assert-LiteralPath $source
    $target = Assert-LiteralPath $target
    $backup = Assert-LiteralPath $backup $false
    $suffix = [Guid]::NewGuid().ToString('N')
    # Unique siblings keep same-volume renames without appending long names to
    # the existing bundle. Both stage and rotated backup must stay short.
    $parent = [IO.Path]::GetDirectoryName($target)
    return @{ source=$source; target=$target; backup=$backup; priorBackup=(Join-Path $parent ('.dp-p-'+$suffix));
              stage=(Join-Path $parent ('.dp-s-'+$suffix)); format=$format; version=$priorVersion;
              backupRotated=$false; originalMoved=$false; installed=$false }
}
)DIRECTPIPE";
    script << R"DIRECTPIPE(
try {
    $plan.downloadedFilePath = Assert-LiteralPath $plan.downloadedFilePath
    $plan.resultFilePath = Assert-LiteralPath $plan.resultFilePath $false
    $null = Assert-LiteralPath ($plan.resultFilePath + '.tmp') $false
    if (-not $plan.skipHostUpdate) {
        $plan.currentExePath = Assert-LiteralPath $plan.currentExePath
        $plan.backupExePath = Assert-LiteralPath $plan.backupExePath $false
        $plan.updatedFlagPath = Assert-LiteralPath $plan.updatedFlagPath $false
    }
    $expected = Get-ReleaseVersion $plan.expectedVersion
    $archiveLock = [IO.File]::Open($plan.downloadedFilePath, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    if ((Get-FileDigest $plan.downloadedFilePath) -ine $plan.expectedPackageSha256) { throw 'The release archive checksum changed before installation' }
    $work = Join-Path ([IO.Path]::GetDirectoryName($plan.downloadedFilePath)) ('directpipe-stage-' + [Guid]::NewGuid().ToString('N'))
    $work = Assert-LiteralPath $work $false
    $null = [IO.Directory]::CreateDirectory($work)
    Expand-VerifiedArchive $plan.downloadedFilePath $work
    $hostCandidates = @(Get-ChildItem -LiteralPath $work -Recurse -File -Filter 'DirectPipe.exe')
    $vst2Candidates = @(Get-ChildItem -LiteralPath $work -Recurse -File -Filter 'DirectPipe Receiver.dll')
    $vst3Candidates = @(Get-ChildItem -LiteralPath $work -Recurse -Directory -Filter 'DirectPipe Receiver.vst3')
    if ($hostCandidates.Count -ne 1 -or $vst2Candidates.Count -ne 1 -or $vst3Candidates.Count -ne 1) { throw 'Expected one host and one complete Receiver for each Windows format' }
    $hostSource = $hostCandidates[0].FullName
    $vst2Source = $vst2Candidates[0].FullName
    $vst3Source = $vst3Candidates[0].FullName
    Assert-Binary $hostSource 'DirectPipe' $plan.expectedVersion ''
    Assert-Binary $vst2Source 'DirectPipe Receiver' $plan.expectedVersion 'VSTPluginMain'
    Assert-Binary (Join-Path $vst3Source 'Contents\x86_64-win\DirectPipe Receiver.vst3') 'DirectPipe Receiver' $plan.expectedVersion 'GetPluginFactory'
    $null = Get-TreeFiles $vst3Source

    if (-not $plan.skipHostUpdate) {
        $null = Assert-LiteralPath $plan.currentExePath
        $currentVersion = [Diagnostics.FileVersionInfo]::GetVersionInfo($plan.currentExePath).FileVersion
        Assert-Binary $plan.currentExePath 'DirectPipe' $currentVersion ''
        if ((Get-ReleaseVersion $currentVersion) -ge $expected) { throw 'Host is current or newer; refusing a host replacement or downgrade' }
        $entries.Add((New-Entry $hostSource $plan.currentExePath $plan.backupExePath 'host' $currentVersion))
    }
    foreach ($target in $plan.targets) {
        $target.path = Assert-LiteralPath $target.path
        $target.binary = Assert-LiteralPath $target.binary
        $export = if ($target.format -eq 'vst2') { 'VSTPluginMain' } else { 'GetPluginFactory' }
        Assert-Binary $target.binary 'DirectPipe Receiver' $target.version $export
        if ((Get-ReleaseVersion $target.version) -ge $expected) { throw 'Receiver is current or newer; refusing replacement or downgrade' }
        $source = if ($target.format -eq 'vst2') { $vst2Source } else { $vst3Source }
        $entries.Add((New-Entry $source $target.path ($target.path+'.directpipe-backup') $target.format $target.version))
    }
    $allPaths = New-Object 'System.Collections.Generic.HashSet[string]' ([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $entries) {
        foreach ($path in @($entry.target, $entry.backup, $entry.stage, $entry.priorBackup)) {
            $full = Assert-LiteralPath $path $false
            if (-not $allPaths.Add($full) -or $full -ieq $plan.downloadedFilePath -or $full -ieq $plan.resultFilePath) { throw 'Overlapping update paths are not allowed' }
        }
        Assert-EntryPaths $entry
    }
    if ($plan.pathPreflightOnly) {
        $result.success = $true
    } else {
    foreach ($entry in $entries) {
        if (Test-Path -LiteralPath $entry.stage) { throw 'Update staging path already exists' }
        Copy-Item -LiteralPath $entry.source -Destination $entry.stage -Recurse -Force
        $product = if ($entry.format -eq 'host') { 'DirectPipe' } else { 'DirectPipe Receiver' }
        $binary = if ($entry.format -eq 'vst3') { Join-Path $entry.stage 'Contents\x86_64-win\DirectPipe Receiver.vst3' } else { $entry.stage }
        $export = if ($entry.format -eq 'vst3') { 'GetPluginFactory' } elseif ($entry.format -eq 'vst2') { 'VSTPluginMain' } else { '' }
        Assert-Binary $binary $product $plan.expectedVersion $export
    }

    # The original DirectPipe instance is the only process we wait for. No
    # OBS/DAW shutdown, process-name matching, taskkill or Restart Manager shutdown.
    if (-not $plan.skipHostUpdate -and $plan.processId -gt 0) {
        while (Get-Process -Id $plan.processId -ErrorAction SilentlyContinue) { Start-Sleep -Milliseconds 250 }
    }
    # Files may have changed while the user was closing the host. Recheck all
    # mapped descendants before acquiring the unchanged all-file protections.
    foreach ($entry in $entries) { Assert-EntryPaths $entry }
)DIRECTPIPE";
    script << R"DIRECTPIPE(
    Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
public static class DirectPipeReceiverLocks {
    [StructLayout(LayoutKind.Sequential)] struct FileInfo {
        public uint attributes; public System.Runtime.InteropServices.ComTypes.FILETIME created, accessed, written;
        public uint volume, sizeHigh, sizeLow, links, indexHigh, indexLow;
    }
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern Microsoft.Win32.SafeHandles.SafeFileHandle CreateFile(string path, uint access, uint share, IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetFileInformationByHandle(Microsoft.Win32.SafeHandles.SafeFileHandle handle, out FileInfo information);
    [StructLayout(LayoutKind.Sequential)] struct UniqueProcess { public uint pid; public System.Runtime.InteropServices.ComTypes.FILETIME started; }
    [StructLayout(LayoutKind.Sequential, CharSet=CharSet.Unicode)] struct ProcessInfo {
        public UniqueProcess process;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=256)] public string app;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=64)] public string service;
        public uint type, status, session; [MarshalAs(UnmanagedType.Bool)] public bool restartable;
    }
    [DllImport("rstrtmgr.dll", CharSet=CharSet.Unicode)] static extern uint RmStartSession(out uint handle, uint flags, StringBuilder key);
    [DllImport("rstrtmgr.dll", CharSet=CharSet.Unicode)] static extern uint RmRegisterResources(uint handle, uint count, string[] files, uint applications, IntPtr apps, uint services, IntPtr names);
    [DllImport("rstrtmgr.dll")] static extern uint RmGetList(uint handle, out uint needed, ref uint count, [In, Out] ProcessInfo[] info, ref uint reason);
    [DllImport("rstrtmgr.dll")] static extern uint RmEndSession(uint handle);
    public static void VerifyPathIdentities(FileStream[] files) {
        foreach (var file in files) {
            FileInfo held, current;
            // Zero desired access can query identity despite our restrictive
            // read sharing. OPEN_REPARSE_POINT prevents following a new link.
            using (var path = CreateFile(file.Name, 0, 7, IntPtr.Zero, 3, 0x00200000, IntPtr.Zero)) {
                if (path.IsInvalid || !GetFileInformationByHandle(path, out current)
                    || !GetFileInformationByHandle(file.SafeFileHandle, out held)
                    || (current.attributes & 0x400) != 0 || held.volume != current.volume
                    || held.indexHigh != current.indexHigh || held.indexLow != current.indexLow)
                    throw new IOException("File identity changed before replacement: " + file.Name);
            }
        }
    }
    public static FileStream[] Acquire(string[] files) {
        uint session, code = RmStartSession(out session, 0, new StringBuilder(33));
        if (code != 0) throw new IOException("File-use inspection failed: " + code);
        try {
            code = RmRegisterResources(session, (uint)files.Length, files, 0, IntPtr.Zero, 0, IntPtr.Zero);
            if (code != 0) throw new IOException("Exact-file registration failed: " + code);
            ProcessInfo[] info = null;
            for (int attempt = 0; attempt < 4; ++attempt) {
                uint count = info == null ? 0 : (uint)info.Length, needed, reasons = 0;
                code = RmGetList(session, out needed, ref count, info, ref reasons);
                if (code == 234 && needed <= 4096) { info = new ProcessInfo[needed]; continue; }
                if (code != 0) throw new IOException("Cannot inspect Receiver use: " + code);
                if (count > 0) throw new IOException("Close " + info[0].app + " (PID " + info[0].process.pid + ") and retry the update.");
                break;
            }
            if (code != 0) throw new IOException("File-use inspection changed repeatedly; retry later.");
            var locks = new List<FileStream>();
            try {
                foreach (var file in files) locks.Add(new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.Delete));
                return locks.ToArray();
            } catch { foreach (var file in locks) file.Dispose(); throw; }
        } finally { RmEndSession(session); }
    }
}
'@
    # Revalidate after the user may have taken time to close the host. Locks
    # cover every bundle file, not just the module. Keep them across renames.
    $files = @()
    $expectedHashes = @{}
    foreach ($entry in $entries) {
        $originalFiles = @(Get-TreeFiles $entry.target)
        if (Test-Path -LiteralPath $entry.backup) { $originalFiles += @(Get-TreeFiles $entry.backup) }
        foreach ($path in $originalFiles) {
            $expectedHashes[$path] = Get-FileDigest $path
            $files += $path
        }
        foreach ($path in @(Get-TreeFiles $entry.stage)) {
            $relative = $path.Substring($entry.stage.Length).TrimStart('\')
            $source = if ($relative) { Join-Path $entry.source $relative } else { $entry.source }
            if (-not $archiveHashes.ContainsKey($source)) { throw "Staged file is not present in the verified archive: $path" }
            $expectedHashes[$path] = $archiveHashes[$source]
            $files += $path
        }
        $product = if ($entry.format -eq 'host') { 'DirectPipe' } else { 'DirectPipe Receiver' }
        $binary = if ($entry.format -eq 'vst3') { Join-Path $entry.target 'Contents\x86_64-win\DirectPipe Receiver.vst3' } else { $entry.target }
        $export = if ($entry.format -eq 'vst3') { 'GetPluginFactory' } elseif ($entry.format -eq 'vst2') { 'VSTPluginMain' } else { '' }
        Assert-Binary $binary $product $entry.version $export
    }
    # DIRECTPIPE_TRANSACTION_BEFORE_LOCK
    $locks = @([DirectPipeReceiverLocks]::Acquire([string[]]$files))
    Assert-LockedHashes ([IO.FileStream[]]$locks) $expectedHashes
    foreach ($entry in $entries) {
        $entryLocks = @($locks | Where-Object {
            $_.Name -ieq $entry.target -or $_.Name.StartsWith($entry.target + '\', [StringComparison]::OrdinalIgnoreCase) -or
            $_.Name -ieq $entry.stage -or $_.Name.StartsWith($entry.stage + '\', [StringComparison]::OrdinalIgnoreCase) -or
            $_.Name -ieq $entry.backup -or $_.Name.StartsWith($entry.backup + '\', [StringComparison]::OrdinalIgnoreCase)
        })
        [DirectPipeReceiverLocks]::VerifyPathIdentities([IO.FileStream[]]$entryLocks)
        $bundleMovedHashes = @{}
        if ($entry.format -eq 'vst3') {
            # Windows refuses a directory rename while any child is open,
            # even with FILE_SHARE_DELETE. Validate, release only this bundle,
            # perform exact renames, then reacquire and validate the moved files.
            # An audio app winning this interval causes an explicit failure;
            # no process is killed and the retained backups remain recoverable.
            foreach ($file in $entryLocks) {
                $oldPath = $file.Name
                if ($oldPath.StartsWith($entry.stage + '\', [StringComparison]::OrdinalIgnoreCase)) {
                    $newPath = $entry.target + $oldPath.Substring($entry.stage.Length)
                } elseif ($oldPath.StartsWith($entry.target + '\', [StringComparison]::OrdinalIgnoreCase)) {
                    $newPath = $entry.backup + $oldPath.Substring($entry.target.Length)
                } else {
                    $newPath = $entry.priorBackup + $oldPath.Substring($entry.backup.Length)
                }
                $bundleMovedHashes[$newPath] = $expectedHashes[$oldPath]
            }
            foreach ($file in $entryLocks) { $file.Dispose() }
            $locks = @($locks | Where-Object { $entryLocks -notcontains $_ })
        }
        if (Test-Path -LiteralPath $entry.backup) {
            Move-Exact $entry.backup $entry.priorBackup
            $entry.backupRotated = $true
        }
        Move-Exact $entry.target $entry.backup
        $entry.originalMoved = $true
        Move-Exact $entry.stage $entry.target
        $entry.installed = $true
        if ($entry.format -eq 'vst3') {
            $movedFiles = @(Get-TreeFiles $entry.target) + @(Get-TreeFiles $entry.backup)
            if ($entry.backupRotated) { $movedFiles += @(Get-TreeFiles $entry.priorBackup) }
            if ($movedFiles.Count -ne $bundleMovedHashes.Count) { throw 'Receiver bundle contents changed during directory replacement' }
            foreach ($path in $movedFiles) {
                if (-not $bundleMovedHashes.ContainsKey($path)) { throw "Unexpected Receiver bundle file after replacement: $path" }
            }
            $reacquired = @([DirectPipeReceiverLocks]::Acquire([string[]]$movedFiles))
            $locks += $reacquired
            Assert-LockedHashes ([IO.FileStream[]]$reacquired) $bundleMovedHashes
        }
        # DIRECTPIPE_TRANSACTION_INSTALLED
    }
    if (-not $plan.skipHostUpdate) {
        $null = Assert-LiteralPath $plan.updatedFlagPath $false
        [IO.File]::WriteAllText($plan.updatedFlagPath, $plan.expectedVersion, [Text.Encoding]::UTF8)
    }
    $result.success = $true
    $result.installed = @($entries | ForEach-Object { $_.target })
    $result.hostUpdated = -not $plan.skipHostUpdate
    foreach ($file in $locks) { $file.Dispose() }; $locks = @()
    }
} catch {
    $result.error = $_.Exception.Message
    # Keep FILE_SHARE_DELETE handles through rollback: renames are allowed,
    # but another audio app must not load a half-installed module meanwhile.
    if (-not $plan.pathPreflightOnly) {
    for ($i = $entries.Count - 1; $i -ge 0; --$i) {
        $entry = $entries[$i]
        try {
            if ($entry.format -eq 'vst3') {
                $bundleLocks = @($locks | Where-Object {
                    $_.Name.StartsWith($entry.target + '\', [StringComparison]::OrdinalIgnoreCase) -or
                    $_.Name.StartsWith($entry.stage + '\', [StringComparison]::OrdinalIgnoreCase) -or
                    $_.Name.StartsWith($entry.backup + '\', [StringComparison]::OrdinalIgnoreCase) -or
                    $_.Name.StartsWith($entry.priorBackup + '\', [StringComparison]::OrdinalIgnoreCase)
                })
                foreach ($file in $bundleLocks) { $file.Dispose() }
                $locks = @($locks | Where-Object { $bundleLocks -notcontains $_ })
            }
            if ($entry.installed) { Remove-OwnedStage $entry.target }
            if ($entry.originalMoved) { Move-Exact $entry.backup $entry.target }
            if ($entry.backupRotated) { Move-Exact $entry.priorBackup $entry.backup }
        } catch { $result.rollbackSucceeded = $false; $result.error += '; rollback requires manual recovery: ' + $_.Exception.Message }
    }
    }
} finally {
    foreach ($file in $locks) { $file.Dispose() }
    if ($archiveLock) { $archiveLock.Dispose() }
    foreach ($entry in $entries) {
        if ($plan.pathPreflightOnly) { continue }
        try { Remove-OwnedStage $entry.stage } catch {}
        if ($result.success) { try { Remove-OwnedStage $entry.priorBackup } catch {} }
    }
    if ($work) { try { Remove-OwnedStage $work } catch {} }
    try {
        $null = Assert-LiteralPath $plan.resultFilePath $false
        $temporaryResult = $plan.resultFilePath + '.tmp'
        $null = Assert-LiteralPath $temporaryResult $false
        [IO.File]::WriteAllText($temporaryResult, ($result | ConvertTo-Json -Depth 5), [Text.Encoding]::UTF8)
        Move-Item -LiteralPath $temporaryResult -Destination $plan.resultFilePath -Force
    } catch { Write-Error ('Cannot record update result: ' + $_.Exception.Message) -ErrorAction Continue }
}
if (-not $plan.pathPreflightOnly -and $plan.relaunchHostAfterUpdate -and -not $plan.skipHostUpdate) {
    try {
        # Dispatch through the existing desktop shell instead of inheriting the
        # elevated updater token in a new audio host.
        if (-not $result.success) {
            Add-Type -AssemblyName System.Windows.Forms
            $message = 'DirectPipe update failed: ' + $result.error
            if ($result.rollbackSucceeded) { $message += "`nThe previous files were preserved or restored." }
            $message += "`nResult details: " + $plan.resultFilePath
            $null = [Windows.Forms.MessageBox]::Show($message, 'DirectPipe Update', 'OK', 'Warning')
        }
        if (-not $result.rollbackSucceeded -or -not (Test-Path -LiteralPath $plan.currentExePath -PathType Leaf)) { throw 'Restore the executable from its retained backup before starting it.' }
        if (-not (Get-Process -Name explorer -ErrorAction SilentlyContinue)) { throw 'The desktop shell is unavailable.' }
        $shellWindows = (New-Object -ComObject Shell.Application).Windows()
        $location = 0; $locationRoot = 0; $desktopHwnd = 0
        # SWC_DESKTOP=8, SWFO_NEEDDISPATCH=1. The desktop browser belongs to
        # Explorer. Its Document.Application dispatches in the existing shell
        # context; a newly constructed Shell.Application alone is insufficient.
        $desktop = $shellWindows.FindWindowSW([ref]$location, [ref]$locationRoot, 8, [ref]$desktopHwnd, 1)
        if (-not $desktop -or -not $desktop.Document.Application) { throw 'Cannot obtain the original desktop shell context.' }
        $desktop.Document.Application.ShellExecute($plan.currentExePath, '', [IO.Path]::GetDirectoryName($plan.currentExePath), 'open', 1)
    } catch {
        $result.relaunchError = $_.Exception.Message
        try {
            Add-Type -AssemblyName System.Windows.Forms
            $null = [Windows.Forms.MessageBox]::Show(('Start DirectPipe manually when ready: ' + $plan.currentExePath + "`n" + $result.relaunchError), 'DirectPipe Update', 'OK', 'Information')
            [IO.File]::WriteAllText($plan.resultFilePath, ($result | ConvertTo-Json -Depth 5), [Text.Encoding]::UTF8)
        } catch {}
    }
}
if (-not $result.success) { Write-Error $result.error -ErrorAction Continue; exit 1 }
exit 0
)DIRECTPIPE";
    return script;
}

} // namespace directpipe::update_detail
