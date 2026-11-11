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

juce::String batchPowerShellLiteral(const juce::String& value)
{
    return "'" + escapePowerShellQuote(escapeBatchPercent(value)) + "'";
}

juce::String powerShellCommand(const juce::String& text)
{
    return "powershell -NoProfile -Command \"" + text + "\"\r\n";
}

bool validStartupPlan(const WindowsUpdateInstallSpec& spec)
{
    if (!spec.requireStartupAck) return true;
    if (spec.skipHostUpdate || !spec.relaunchHostAfterUpdate || spec.startupAckTimeoutMs <= 0
        || spec.startupAttemptId.length() != 32
        || !spec.startupAttemptId.containsOnly("0123456789abcdef")
        || spec.startupAttemptId.containsOnly("0")
        || !juce::File::isAbsolutePath(spec.resultFilePath)
        || !juce::File::isAbsolutePath(spec.startupAckFilePath)) return false;
    const juce::File receipt(spec.resultFilePath);
    return juce::File(spec.startupAckFilePath) == receipt.getSiblingFile(
        receipt.getFileNameWithoutExtension() + "." + spec.startupAttemptId + ".startup.json");
}

juce::String desktopHostRelaunchCommand(const juce::String& executable,
                                       const juce::String& arguments = {})
{
    return "$ErrorActionPreference='Stop'; $windows=(New-Object -ComObject Shell.Application).Windows(); "
        "$location=0;$root=0;$hwnd=0;$desktop=$windows.FindWindowSW([ref]$location,[ref]$root,8,[ref]$hwnd,1); "
        "if(-not $desktop -or -not $desktop.Document.Application){throw 'The original desktop shell is unavailable'}; "
        "$desktop.Document.Application.ShellExecute(" + batchPowerShellLiteral(executable)
        + "," + batchPowerShellLiteral(arguments) + ","
        + batchPowerShellLiteral(juce::File(executable).getParentDirectory().getFullPathName())
        + ",'open',1)";
}

// Shared only by the two existing Windows host update paths. This is startup
// evidence at one point in time; it does not inspect audio or long-term health.
juce::String startupAcknowledgementFunctions()
{
    return R"DIRECTPIPE(
function Get-StartupDigest([string]$path) {
    $stream = [IO.File]::OpenRead($path)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose(); $stream.Dispose() }
}
function Save-StartupReceipt($plan, $result) {
    $temporary = $plan.resultFilePath + '.tmp'
    [IO.File]::WriteAllText($temporary, ($result | ConvertTo-Json -Depth 6), [Text.Encoding]::UTF8)
    if ([IO.File]::Exists($plan.resultFilePath)) {
        [IO.File]::Replace($temporary, $plan.resultFilePath, [System.Management.Automation.Language.NullString]::Value)
    } else { [IO.File]::Move($temporary, $plan.resultFilePath) }
}
function Start-UpdatedDesktopHost($plan) {
    $windows = (New-Object -ComObject Shell.Application).Windows()
    $location = 0; $root = 0; $hwnd = 0
    $desktop = $windows.FindWindowSW([ref]$location, [ref]$root, 8, [ref]$hwnd, 1)
    if (-not $desktop -or -not $desktop.Document.Application) { throw 'Cannot obtain the original desktop shell context. Start DirectPipe manually and keep its recovery files.' }
    $arguments = '--directpipe-update-attempt=' + $plan.startupAttemptId
    $desktop.Document.Application.ShellExecute($plan.currentExePath, $arguments, [IO.Path]::GetDirectoryName($plan.currentExePath), 'open', 1)
}
function Assert-StartupCandidate($plan, $result) {
    if (-not $result.candidateHash -or (Get-StartupDigest $plan.currentExePath) -ine $result.candidateHash) { throw 'The installed candidate hash changed before startup could be confirmed' }
    $identity = [Diagnostics.FileVersionInfo]::GetVersionInfo($plan.currentExePath)
    if ($identity.FileVersion -cne $plan.expectedVersion -or $identity.ProductVersion -cne $plan.expectedVersion) { throw 'The installed candidate version changed before startup could be confirmed' }
}
function Wait-VerifiedHostStartup($plan, $result) {
    $result.stage = 'startup-wait'
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.ElapsedMilliseconds -lt $plan.startupAckTimeoutMs) {
        if ([IO.File]::Exists($plan.startupAckFilePath)) {
            $ack = [IO.File]::ReadAllText($plan.startupAckFilePath, [Text.Encoding]::UTF8) | ConvertFrom-Json
            if ($ack.attemptId -cne $plan.startupAttemptId) { throw 'Startup acknowledgement belongs to another update attempt' }
            if ([string]::IsNullOrWhiteSpace($ack.currentExePath) -or [IO.Path]::GetFullPath($ack.currentExePath) -ine [IO.Path]::GetFullPath($plan.currentExePath)) { throw 'Startup acknowledgement executable path does not match this installation' }
            if ($ack.version -cne $plan.expectedVersion) { throw 'Startup acknowledgement version does not match the installed candidate' }
            if ($ack.ready -isnot [bool]) { throw 'Startup acknowledgement readiness is invalid' }
            if (-not $ack.ready) { throw ('DirectPipe reported incomplete startup: ' + [string]$ack.reason) }
            [long]$ackPid = 0
            if (-not [long]::TryParse([string]$ack.pid, [ref]$ackPid) -or $ackPid -le 0 -or $ackPid -gt [int]::MaxValue) { throw 'Startup acknowledgement process identity is invalid' }
            $candidateProcess = $null
            try {
                $candidateProcess = [Diagnostics.Process]::GetProcessById([int]$ackPid)
                if ($candidateProcess.HasExited -or [IO.Path]::GetFullPath($candidateProcess.MainModule.FileName) -ine [IO.Path]::GetFullPath($plan.currentExePath)) { throw 'Startup acknowledgement PID is not the running installed candidate' }
                Assert-StartupCandidate $plan $result
                $remaining = [long]$plan.startupAckTimeoutMs - $clock.ElapsedMilliseconds
                if ($remaining -lt 500) { throw 'Startup acknowledgement arrived too late to verify the running candidate' }
                if ($candidateProcess.WaitForExit(500)) { throw 'The candidate exited before startup could be confirmed' }
                $candidateProcess.Refresh()
                if ($candidateProcess.HasExited) { throw 'The candidate exited before startup could be confirmed' }
                Assert-StartupCandidate $plan $result
                $result.startupReady = $true
                $result.startupPid = $ackPid
                return
            } finally { if ($candidateProcess) { $candidateProcess.Dispose() } }
        }
        Start-Sleep -Milliseconds 50
    }
    throw 'Timed out waiting for verified DirectPipe startup. The new files are committed; keep the retained recovery files and check the update log.'
}
function Remove-VerifiedHostBackups($result, [string]$backup, [string]$prior) {
    $result.stage = 'startup-cleanup'
    # Validate every identity before deleting any member of this attempt.
    if (-not $result.currentBeforeHash -or -not [IO.File]::Exists($backup) -or (Get-StartupDigest $backup) -ine $result.currentBeforeHash) { throw 'The managed executable backup changed; it was retained for manual recovery' }
    if ($result.backupBeforeHash) {
        if (-not [IO.File]::Exists($prior) -or (Get-StartupDigest $prior) -ine $result.backupBeforeHash) { throw 'The prior managed backup changed; recovery files were retained' }
    } elseif ([IO.File]::Exists($prior) -or [IO.Directory]::Exists($prior)) { throw 'An unexpected prior backup exists; recovery files were retained' }
    if ($result.backupBeforeHash) { [IO.File]::Delete($prior) }
    [IO.File]::Delete($backup)
    $result.backupCleanupSucceeded = $true
}
)DIRECTPIPE";
}

juce::String startupPlanJson(const WindowsUpdateInstallSpec& spec)
{
    auto plan = std::make_unique<juce::DynamicObject>();
    plan->setProperty("currentExePath", spec.currentExePath);
    plan->setProperty("backupExePath", spec.backupExePath);
    plan->setProperty("previousBackupExePath", spec.previousBackupExePath.isNotEmpty()
        ? spec.previousBackupExePath : spec.backupExePath + ".previous");
    plan->setProperty("expectedVersion", spec.expectedVersion);
    plan->setProperty("resultFilePath", spec.resultFilePath);
    plan->setProperty("updatedFlagPath", spec.updatedFlagPath);
    plan->setProperty("startupAttemptId", spec.startupAttemptId);
    plan->setProperty("startupAckFilePath", spec.startupAckFilePath);
    plan->setProperty("startupAckTimeoutMs", spec.startupAckTimeoutMs);
    return juce::JSON::toString(juce::var(plan.release()), true);
}

juce::String hostStartupPayload(const WindowsUpdateInstallSpec& spec)
{
    const auto json = startupPlanJson(spec);
    juce::String script = "$ErrorActionPreference='Stop'\nSet-StrictMode -Version 2\n";
    script << "$plan=[Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('"
           << juce::Base64::toBase64(json.toRawUTF8(), json.getNumBytesAsUTF8()) << "'))|ConvertFrom-Json\n";
    script << startupAcknowledgementFunctions();
    script << R"DIRECTPIPE(
$result = @{ success=$false; filesCommitted=$true; inProgress=$false; startupReady=$false; stage='startup-wait';
    requireStartupAck=$true; startupAttemptId=$plan.startupAttemptId; startupAckFilePath=$plan.startupAckFilePath;
    currentExePath=$plan.currentExePath; expectedVersion=$plan.expectedVersion; error=''; relaunchError='';
    rollbackAttempted=$false; backupCleanupSucceeded=$false }
try {
    $pending = [IO.File]::ReadAllText($plan.resultFilePath, [Text.Encoding]::UTF8) | ConvertFrom-Json
    foreach ($property in $pending.PSObject.Properties) { $result[$property.Name] = $property.Value }
    if (-not $result.requireStartupAck -or -not $result.filesCommitted -or -not $result.inProgress -or
        $result.startupAttemptId -cne $plan.startupAttemptId -or $result.startupAckFilePath -ine $plan.startupAckFilePath -or
        $result.currentExePath -ine $plan.currentExePath -or $result.expectedVersion -cne $plan.expectedVersion) { throw 'The pending update receipt does not match this startup attempt' }
    Start-UpdatedDesktopHost $plan
    Wait-VerifiedHostStartup $plan $result
    $result.stage = 'startup-cleanup'
    Save-StartupReceipt $plan $result
    Remove-VerifiedHostBackups $result $plan.backupExePath $plan.previousBackupExePath
    $result.stage = 'complete'
    if ($plan.updatedFlagPath) { [IO.File]::WriteAllText($plan.updatedFlagPath, $plan.expectedVersion, [Text.Encoding]::UTF8) }
    $result.success = $true; $result.inProgress = $false; $result.error = ''; $result.relaunchError = ''; $result.nextCheck = ''
} catch {
    $result.success = $false; $result.inProgress = $false
    $result.error = $_.Exception.Message
    $result.relaunchError = if ($result.startupReady) { '' } else { $result.error }
    $result.nextCheck = 'The replacement files are committed. Keep retained backups; check the startup error and update log. No automatic rollback was attempted under the candidate.'
    Write-Error ('[update] ' + $result.stage + ': ' + $result.error) -ErrorAction Continue
}
try { Save-StartupReceipt $plan $result }
catch { Write-Error ('Cannot record startup result: ' + $_.Exception.Message) -ErrorAction Continue; exit 7 }
if (-not $result.success) { exit 2 }
exit 0
)DIRECTPIPE";
    return script;
}

juce::String hostResultCommand(const WindowsUpdateInstallSpec& spec, bool snapshot, bool success,
                               bool startupPending = false)
{
    if (spec.resultFilePath.isEmpty()) return {};
    const auto file = batchPowerShellLiteral(spec.resultFilePath);
    const auto current = batchPowerShellLiteral(spec.currentExePath);
    const auto backup = batchPowerShellLiteral(spec.backupExePath);
    const auto prior = batchPowerShellLiteral(spec.previousBackupExePath.isNotEmpty()
        ? spec.previousBackupExePath : spec.backupExePath + ".previous");
    juce::String ps = "$ErrorActionPreference='Stop'; function D([string]$p) { "
        "if (-not [IO.File]::Exists($p)) { return $null }; $s=[IO.File]::OpenRead($p); "
        "$h=[Security.Cryptography.SHA256]::Create(); try { "
        "return [BitConverter]::ToString($h.ComputeHash($s)).Replace('-','') "
        "} finally { $h.Dispose(); $s.Dispose() } }; try { $f=" + file + "; ";
    if (snapshot) {
        ps << "$r=@{success=$false;inProgress=$true;stage='prepare';error='';rollbackSucceeded=$true;"
              "currentBeforeHash=(D " << current << ");backupBeforeHash=(D " << backup << ");"
              "originalStateVerified=$true;currentExePath=" << current << ";logFilePath="
           << batchPowerShellLiteral(spec.logFilePath) << ";expectedVersion='" << spec.expectedVersion << "';"
              "filesCommitted=$false;startupReady=$false;backupCleanupSucceeded=$false;requireStartupAck="
           << (spec.requireStartupAck ? "$true" : "$false") << ";startupAttemptId='" << spec.startupAttemptId
           << "';startupAckFilePath=" << batchPowerShellLiteral(spec.startupAckFilePath) << "}; ";
    } else {
        ps << "$r=if ([IO.File]::Exists($f)) { [IO.File]::ReadAllText($f)|ConvertFrom-Json } "
              "else { [pscustomobject]@{originalStateVerified=$false} }; "
              "$v=@{}; $r.PSObject.Properties|ForEach-Object{$v[$_.Name]=$_.Value}; "
              "$v.success=" << (success && !startupPending ? "$true" : "$false") << "; $v.inProgress="
           << (startupPending ? "$true" : "$false") << "; $v.filesCommitted="
           << (success ? "($env:DP_REPLACEMENT_INSTALLED -eq '1')" : "$false") << "; "
              "$v.stage=$env:DP_UPDATE_STAGE; $v.error="
           << (success ? "''" : "$env:DP_UPDATE_ERROR") << "; "
              "$v.rollbackSucceeded=($env:DP_ROLLBACK_FAILED -ne '1'); "
              "$v.currentRestored=$null; $v.previousBackupRestored=$null; "
              "if (-not $v.success -and -not $v.inProgress -and $v.originalStateVerified) { "
              "$v.currentRestored=((D " << current << ") -eq $v.currentBeforeHash); "
              "$v.previousBackupRestored=((D " << backup << ") -eq $v.backupBeforeHash); "
              "$v.rollbackSucceeded=$v.rollbackSucceeded -and $v.currentRestored -and $v.previousBackupRestored }; "
              "$v.nextCheck=if($v.success){''}else{'Check the update log, file locks and write permissions. Keep retained recovery files.'}; "
              "$v.hostUpdated=$v.filesCommitted; $v.relaunchError=$env:DP_RELAUNCH_ERROR; $v.logFilePath="
           << batchPowerShellLiteral(spec.logFilePath) << "; "
              "$v.recoveryPaths=@(" << current << "," << backup << "," << prior << "); ";
        if (startupPending)
            ps << "$v.candidateHash=(D " << current << "); $v.rollbackAttempted=$false; ";
        ps << "$r=$v; ";
    }
    ps << "$t=$f+'.tmp'; [IO.File]::WriteAllText($t,($r|ConvertTo-Json -Depth 5),[Text.Encoding]::UTF8); "
          "if([IO.File]::Exists($f)){[IO.File]::Replace($t,$f,[System.Management.Automation.Language.NullString]::Value)}else{[IO.File]::Move($t,$f)}; "
          "} catch { Write-Error ('Cannot record update result: '+$_.Exception.Message) -ErrorAction Continue; exit 7 }";
    return powerShellCommand(ps);
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

juce::String buildWindowsUpdateWaitScript(unsigned long processId, int timeoutMs,
                                         const juce::String& resultFilePath,
                                         const juce::String& logFilePath)
{
    if (processId == 0)
        return {};
    if (timeoutMs <= 0) return "exit /b 3\r\n";
    juce::String script;
    script << "echo [update] wait: Waiting for this DirectPipe instance to close...\r\n";
    juce::String ps = "$ErrorActionPreference='Stop'; $targetProcess=$null; try { ";
    if (resultFilePath.isNotEmpty())
        ps << "$waitResult=@{success=$false;inProgress=$true;stage='wait';logFilePath="
           << batchPowerShellLiteral(logFilePath) << "}; $receipt=" << batchPowerShellLiteral(resultFilePath)
           << "; if([IO.File]::Exists($receipt)){try{$previous=[IO.File]::ReadAllText($receipt)|ConvertFrom-Json;"
              "$waitResult.currentExePath=$previous.currentExePath;$waitResult.expectedVersion=$previous.expectedVersion}catch{}}; "
              "[IO.File]::WriteAllText($receipt,($waitResult|ConvertTo-Json),[Text.Encoding]::UTF8); ";
    ps << "$targetProcess=Get-Process -Id " << juce::String(processId)
       << " -ErrorAction SilentlyContinue; if($targetProcess -and -not $targetProcess.WaitForExit("
       << juce::String(timeoutMs) << ")) { throw 'Timed out waiting for the launching DirectPipe process. Close that instance and retry; no installed files were changed.' }; "
          "} catch { $message=$_.Exception.Message; ";
    if (resultFilePath.isNotEmpty())
        ps << "try { $r=@{success=$false;inProgress=$false;stage='wait';error=$message;rollbackSucceeded=$true;"
              "nextCheck='Close the original DirectPipe instance and retry. No process was terminated.';logFilePath="
           << batchPowerShellLiteral(logFilePath) << "}; if($waitResult){$r.currentExePath=$waitResult.currentExePath;$r.expectedVersion=$waitResult.expectedVersion}; "
              "[IO.File]::WriteAllText(" << batchPowerShellLiteral(resultFilePath)
           << ",($r|ConvertTo-Json),[Text.Encoding]::UTF8) } catch { Write-Warning 'Cannot record wait result' }; ";
    if (logFilePath.isNotEmpty())
        ps << "try { [IO.File]::AppendAllText(" << batchPowerShellLiteral(logFilePath)
           << ",'[update] wait failed: '+$message+[Environment]::NewLine) } catch {}; ";
    ps << "Write-Error $message -ErrorAction Continue; exit 3 } finally { if($targetProcess){$targetProcess.Dispose()} }";
    script << powerShellCommand(ps);
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

    auto paths = spec;
    paths.expectedVersion = canonicalExpectedVersion;
    const auto previous = spec.previousBackupExePath.isNotEmpty()
        ? spec.previousBackupExePath : spec.backupExePath + ".previous";
    for (const auto& path : {spec.currentExePath, spec.downloadedFilePath, spec.stagedExePath,
                             spec.backupExePath, spec.updateDirPath, previous}) {
        if (path.isEmpty() || path.containsAnyOf("\r\n\"")) return {};
    }
    if (previous == spec.currentExePath || previous == spec.downloadedFilePath
        || previous == spec.stagedExePath || previous == spec.backupExePath) return {};
    paths.currentExePath = escapeBatchPercent(spec.currentExePath);
    paths.downloadedFilePath = escapeBatchPercent(spec.downloadedFilePath);
    paths.stagedExePath = escapeBatchPercent(spec.stagedExePath);
    paths.backupExePath = escapeBatchPercent(spec.backupExePath);
    paths.updateDirPath = escapeBatchPercent(spec.updateDirPath);
    const auto priorPath = escapeBatchPercent(previous);
    juce::String script;
    const auto stage = [&](const char* value, const char* error) {
        script << "set \"DP_UPDATE_STAGE=" << value << "\"\r\n"
               << "set \"DP_UPDATE_ERROR=" << error << "\"\r\n"
               << "echo [update] " << value << "\r\n";
    };
    const auto moveExact = [&](const juce::String& from, const juce::String& to) {
        script << powerShellCommand("$ErrorActionPreference='Stop'; [IO.File]::Move('"
            + escapePowerShellQuote(from) + "','" + escapePowerShellQuote(to) + "')");
    };
    script << "set \"DP_PRIOR_ROTATED=0\"\r\nset \"DP_ORIGINAL_MOVED=0\"\r\n"
              "set \"DP_REPLACEMENT_INSTALLED=0\"\r\nset \"DP_ROLLBACK_FAILED=0\"\r\n";
    stage("preflight", "The installed executable is missing or diagnostics could not be prepared.");
    script << "if not exist \"" << paths.currentExePath << "\" goto update_install_failed\r\n";
    script << hostResultCommand(spec, true, false);
    if (spec.resultFilePath.isNotEmpty()) script << "if errorlevel 1 goto update_install_failed\r\n";
    juce::String installSource = paths.downloadedFilePath;
    if (spec.expectedPackageSha256.isNotEmpty()) {
        if (spec.expectedPackageSha256.length() != 64
            || !spec.expectedPackageSha256.containsOnly("0123456789abcdefABCDEF")) return {};
        stage("validate", "The downloaded package checksum could not be verified.");
        script << powerShellCommand("$ErrorActionPreference='Stop'; $s=[IO.File]::OpenRead('"
            + escapePowerShellQuote(paths.downloadedFilePath)
            + "'); $h=[Security.Cryptography.SHA256]::Create(); try { "
              "$actual=[BitConverter]::ToString($h.ComputeHash($s)).Replace('-',''); "
              "if($actual -ine '" + spec.expectedPackageSha256 + "'){throw 'Update package checksum mismatch'} "
              "} finally {$h.Dispose();$s.Dispose()}");
        script << "if errorlevel 1 goto update_install_failed\r\n";
    }
    if (paths.isZip) {
        stage("extract", "The update archive could not be staged. Keep existing staging files and check the log.");
        // A previous attempt's payload is recovery evidence, never disposable
        // preflight cleanup. Production callers use a private fresh work folder.
        script << "if exist \"" << paths.updateDirPath << "\" goto update_install_failed\r\n"
               << "if exist \"" << paths.stagedExePath << "\" goto update_install_failed\r\n";
        script << powerShellCommand("$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath '"
            + escapePowerShellQuote(paths.downloadedFilePath) + "' -DestinationPath '"
            + escapePowerShellQuote(paths.updateDirPath) + "' -Force");
        script << "if errorlevel 1 goto update_install_failed\r\n";
        stage("select", "The archive must contain exactly one DirectPipe.exe.");
        script << powerShellCommand("$ErrorActionPreference='Stop'; $candidates=@(Get-ChildItem -LiteralPath '"
            + escapePowerShellQuote(paths.updateDirPath) + "' -Recurse -File -Filter 'DirectPipe.exe'); "
              "if($candidates.Count -ne 1){throw ('Expected exactly one DirectPipe.exe in update archive; found '+$candidates.Count)}; "
              "$null=[IO.Directory]::CreateDirectory('"
            + escapePowerShellQuote(escapeBatchPercent(juce::File(spec.stagedExePath).getParentDirectory().getFullPathName()))
            + "'); Copy-Item -LiteralPath $candidates[0].FullName -Destination '"
            + escapePowerShellQuote(paths.stagedExePath) + "' -Force");
        script << "if errorlevel 1 goto update_install_failed\r\n";
        installSource = paths.stagedExePath;
    } else if (juce::File(spec.downloadedFilePath).getParentDirectory()
               != juce::File(spec.currentExePath).getParentDirectory()) {
        stage("stage", "The standalone update could not be staged beside the installed executable.");
        script << "if exist \"" << paths.stagedExePath << "\" goto update_install_failed\r\n";
        script << powerShellCommand("$ErrorActionPreference='Stop'; $null=[IO.Directory]::CreateDirectory('"
            + escapePowerShellQuote(escapeBatchPercent(juce::File(spec.stagedExePath).getParentDirectory().getFullPathName()))
            + "'); Copy-Item -LiteralPath '" + escapePowerShellQuote(paths.downloadedFilePath)
            + "' -Destination '" + escapePowerShellQuote(paths.stagedExePath) + "' -Force");
        script << "if errorlevel 1 goto update_install_failed\r\n";
        installSource = paths.stagedExePath;
    }
    stage("validate", "The candidate executable version could not be validated. Check its FileVersion and ProductVersion.");
    script << powerShellCommand("$ErrorActionPreference='Stop'; $v=(Get-Item -LiteralPath '"
        + escapePowerShellQuote(installSource) + "').VersionInfo; if(($v.FileVersion -ne '"
        + paths.expectedVersion + "') -or ($v.ProductVersion -ne '" + paths.expectedVersion
        + "')){throw ('Update executable version mismatch: FileVersion='+$v.FileVersion+', ProductVersion='+$v.ProductVersion)}");
    script << "if errorlevel 1 goto update_install_failed\r\n";
    stage("reserve-backup", "The previous backup could not be reserved. Keep retained backup files and check locks.");
    script << "if exist \"" << priorPath << "\" goto update_install_failed\r\n"
           << "if not exist \"" << paths.backupExePath << "\" goto update_backup_reserved\r\n";
    moveExact(paths.backupExePath, priorPath);
    script << "if errorlevel 1 goto update_install_failed\r\nset \"DP_PRIOR_ROTATED=1\"\r\n:update_backup_reserved\r\n";
    stage("move-original", "The installed executable could not be moved. Close remaining instances or applications locking it, then retry.");
    moveExact(paths.currentExePath, paths.backupExePath);
    script << "if errorlevel 1 goto update_install_rollback\r\nset \"DP_ORIGINAL_MOVED=1\"\r\n";
    stage("replace", "The staged executable could not replace the installed file. Check file locks and write permissions.");
    script << "rem DIRECTPIPE_HOST_BEFORE_REPLACE\r\n";
    moveExact(installSource, paths.currentExePath);
    script << "if errorlevel 1 goto update_install_rollback\r\nset \"DP_REPLACEMENT_INSTALLED=1\"\r\n";
    stage("verify", "The installed candidate failed its post-replacement version check.");
    script << powerShellCommand("$ErrorActionPreference='Stop'; $v=(Get-Item -LiteralPath '"
        + escapePowerShellQuote(paths.currentExePath) + "').VersionInfo; if(($v.FileVersion -ne '"
        + paths.expectedVersion + "') -or ($v.ProductVersion -ne '" + paths.expectedVersion
        + "')){throw 'Installed executable version mismatch'}");
    script << "if errorlevel 1 goto update_install_rollback\r\n";
    stage(spec.requireStartupAck ? "startup-wait" : "complete",
          "The update committed but its diagnostic result could not be saved.");
    script << hostResultCommand(spec, false, true, spec.requireStartupAck);
    if (spec.resultFilePath.isNotEmpty()) script << "if errorlevel 1 goto update_install_rollback\r\n";
    // Only committed success can dispose of this operation's old backup/stage.
    if (!spec.requireStartupAck)
        script << "if \"%DP_PRIOR_ROTATED%\"==\"1\" del /f /q \"" << priorPath << "\"\r\n";
    if (paths.isZip && !spec.requireStartupAck)
        script << "rd /s /q \"" << paths.updateDirPath << "\"\r\n"
               << "del /f /q \"" << paths.downloadedFilePath << "\"\r\n";
    script << "goto update_install_complete\r\n:update_install_rollback\r\n"
              "echo [update] rollback\r\n"
              "if \"%DP_REPLACEMENT_INSTALLED%\"==\"0\" goto update_restore_current\r\n";
    // Move a failed installed payload back to staging; never discard it during
    // rollback, and never overwrite another retained candidate.
    script << "if exist \"" << installSource << "\" goto update_payload_restore_failed\r\n";
    moveExact(paths.currentExePath, installSource);
    script << "if errorlevel 1 goto update_payload_restore_failed\r\ngoto update_restore_current\r\n"
              ":update_payload_restore_failed\r\nset \"DP_ROLLBACK_FAILED=1\"\r\n"
              ":update_restore_current\r\n"
              "if \"%DP_ORIGINAL_MOVED%\"==\"0\" goto update_restore_prior_backup\r\n"
              "if exist \"" << paths.currentExePath << "\" goto update_current_restore_failed\r\n";
    moveExact(paths.backupExePath, paths.currentExePath);
    script << "if errorlevel 1 goto update_current_restore_failed\r\ngoto update_restore_prior_backup\r\n"
              ":update_current_restore_failed\r\nset \"DP_ROLLBACK_FAILED=1\"\r\n"
              ":update_restore_prior_backup\r\n"
              "if \"%DP_PRIOR_ROTATED%\"==\"0\" goto update_install_failed\r\n"
              "if exist \"" << paths.backupExePath << "\" goto update_backup_restore_failed\r\n";
    moveExact(priorPath, paths.backupExePath);
    script << "if errorlevel 1 goto update_backup_restore_failed\r\ngoto update_install_failed\r\n"
              ":update_backup_restore_failed\r\nset \"DP_ROLLBACK_FAILED=1\"\r\n"
              ":update_install_failed\r\n"
              "echo [update] failed at %DP_UPDATE_STAGE%: %DP_UPDATE_ERROR%\r\n";
    script << hostResultCommand(spec, false, false);
    script << "exit /b 1\r\n:update_install_complete\r\n";
    return script;
}

juce::String buildWindowsHostUpdateScript(const WindowsUpdateInstallSpec& spec)
{
    if (spec.processWaitTimeoutMs <= 0 || !validStartupPlan(spec)) return {};
    const auto install = buildWindowsUpdateInstallScript(spec);
    if (install.isEmpty() || spec.skipHostUpdate)
        return {};
    juce::String script = "chcp 65001 > nul\r\nsetlocal DisableDelayedExpansion\r\n"
                          "set \"DP_UPDATE_SELF=%~f0\"\r\n";
    if (spec.logFilePath.isNotEmpty()) {
        script << "if \"%~1\"==\"--directpipe-update-inner\" goto directpipe_update_main\r\n"
                  "set \"DP_UPDATE_SELF=%~f0\"\r\n"
                  "cmd.exe /d /v:off /c \"\"%%DP_UPDATE_SELF%%\" --directpipe-update-inner\" >\""
               << escapeBatchPercent(spec.logFilePath) << "\" 2>&1\r\n"
                  "exit /b %errorlevel%\r\n:directpipe_update_main\r\n";
    }
    script << "set \"DP_RELAUNCH_ERROR=\"\r\nset \"DP_UPDATE_STAGE=wait\"\r\n"
              "set \"DP_UPDATE_ERROR=The launching process wait failed.\"\r\n";
    script << buildWindowsUpdateWaitScript(spec.processId, spec.processWaitTimeoutMs,
                                           spec.resultFilePath, spec.logFilePath)
        .replace("if errorlevel 1 exit /b 3", "if errorlevel 1 goto host_update_failed");
    script << install.replace("exit /b 1\r\n:update_install_complete",
                              "goto host_update_failed\r\n:update_install_complete");
    if (spec.requireStartupAck) {
        // Keep the full BAT protocol while avoiding cmd.exe's 8191-character
        // command-line limit. The bounded PS payload is delimited below every
        // BAT exit and is read literally from this already-admitted script.
        script << "echo [update] startup-wait\r\n";
        script << powerShellCommand("$ErrorActionPreference='Stop'; $text=[IO.File]::ReadAllText($env:DP_UPDATE_SELF,[Text.Encoding]::UTF8); "
            "$begin='# DIRECTPIPE_STARTUP_PS_BEGIN'; $end='# DIRECTPIPE_STARTUP_PS_END'; "
            "$first=$text.LastIndexOf($begin); $last=$text.LastIndexOf($end); "
            "if($first -lt 0 -or $last -le $first){throw 'Startup verification payload is missing'}; "
            "& ([scriptblock]::Create($text.Substring($first+$begin.Length,$last-$first-$begin.Length)))");
        script << "if errorlevel 1 goto update_startup_attention\r\nexit /b 0\r\n"
                  ":update_startup_attention\r\n"
                  "set \"DP_RELAUNCH_ERROR=The executable replacement committed, but verified startup or recovery-file cleanup needs attention. Keep retained recovery files and check the update log/result.\"\r\n"
                  "goto host_update_completion_attention\r\n";
    }
    if (spec.updatedFlagPath.isNotEmpty()) {
        script << "echo " << spec.expectedVersion << " > \""
               << escapeBatchPercent(spec.updatedFlagPath) << "\"\r\n"
                  "if errorlevel 1 goto update_completion_failed\r\n";
    }
    if (spec.relaunchHostAfterUpdate) {
        script << "echo [update] relaunch\r\n";
        script << powerShellCommand(desktopHostRelaunchCommand(spec.currentExePath,
            spec.requireStartupAck ? "--directpipe-update-attempt=" + spec.startupAttemptId : juce::String()));
        script << "if errorlevel 1 goto update_completion_failed\r\n";
    }
    script << "goto update_completion_done\r\n:update_completion_failed\r\n"
              "set \"DP_RELAUNCH_ERROR=The executable was updated, but completion or restart failed. Start DirectPipe manually and check the update log.\"\r\n"
              "echo [update] completion or relaunch failed\r\n:update_completion_done\r\n";
    script << "rem DIRECTPIPE_HOST_BEFORE_COMPLETION_RESULT\r\n";
    script << hostResultCommand(spec, false, true);
    if (spec.resultFilePath.isNotEmpty())
        script << "if errorlevel 1 goto update_completion_record_failed\r\n";
    script << "if not \"%DP_RELAUNCH_ERROR%\"==\"\" goto host_update_completion_attention\r\n"
              "exit /b 0\r\n:update_completion_record_failed\r\n"
              "set \"DP_RELAUNCH_ERROR=Executable files committed, but the final update result could not be saved. Check the update log and start DirectPipe manually if needed.\"\r\n"
              ":host_update_completion_attention\r\n"
              "echo [update] files committed; completion needs attention: %DP_RELAUNCH_ERROR%\r\n";
    if (spec.relaunchHostAfterUpdate && spec.showInteractiveErrors) {
        juce::String notice = "try { ";
        if (spec.requireStartupAck)
            notice << "$confirmed=$false; try{$r=[IO.File]::ReadAllText("
                   << batchPowerShellLiteral(spec.resultFilePath)
                   << ",[Text.Encoding]::UTF8)|ConvertFrom-Json;$confirmed=[bool]$r.startupReady}catch{}; if(-not $confirmed){ ";
        notice << "Add-Type -AssemblyName System.Windows.Forms; "
            "$message='The DirectPipe executable replacement committed, but completion or restart needs attention.'"
            "+[Environment]::NewLine+$env:DP_RELAUNCH_ERROR+[Environment]::NewLine+'Log: '+"
            + batchPowerShellLiteral(spec.logFilePath) + "+[Environment]::NewLine+'Result: '+"
            + batchPowerShellLiteral(spec.resultFilePath)
            + "; $null=[Windows.Forms.MessageBox]::Show($message,'DirectPipe Update','OK','Warning') ";
        if (spec.requireStartupAck) notice << "} ";
        notice << "} catch { Write-Warning 'Could not display completion error; check the update log' }";
        script << powerShellCommand(notice);
    }
    script << "exit /b 2\r\n:host_update_failed\r\n";
    if (spec.relaunchHostAfterUpdate && spec.showInteractiveErrors) {
        // The real hidden installer must explain failure immediately. Fixtures
        // opt out through the existing relaunch=false setting and never show UI.
        juce::String notice = "$ErrorActionPreference='Stop'; try { $r=$null; try { $r=[IO.File]::ReadAllText("
            + batchPowerShellLiteral(spec.resultFilePath) + ")|ConvertFrom-Json } catch {}; "
              "$stage=if($env:DP_UPDATE_STAGE){$env:DP_UPDATE_STAGE}elseif($r){$r.stage}else{'unknown'}; "
              "$receiptFinal=$r -and -not $r.inProgress -and $r.stage -eq $stage; "
              "$reason=if($receiptFinal -and $r.error){$r.error}else{$env:DP_UPDATE_ERROR}; "
              "$verified=$receiptFinal -and $r.originalStateVerified -and $r.rollbackSucceeded -and $r.currentRestored -and $r.previousBackupRestored; "
              "$message='DirectPipe update failed at '+$stage+': '+$reason+[Environment]::NewLine; "
              "if($verified){$message+='The current executable and previous backup were verified restored.'} "
              "elseif($stage -eq 'wait'){$message+='The waiting step did not replace installed files.'} "
              "else{$message+='Complete restoration is not verified. Keep all recovery files and check their paths.'}; "
              "$message+=[Environment]::NewLine+'Log: ' + " + batchPowerShellLiteral(spec.logFilePath)
            + "+[Environment]::NewLine+'Result: '+" + batchPowerShellLiteral(spec.resultFilePath)
            + "+[Environment]::NewLine+'Current/backup recovery: '+" + batchPowerShellLiteral(spec.currentExePath)
            + "+' / '+" + batchPowerShellLiteral(spec.backupExePath)
            + "; Add-Type -AssemblyName System.Windows.Forms; $null=[Windows.Forms.MessageBox]::Show($message,'DirectPipe Update','OK','Warning'); ";
        if (spec.processId > 0)
            notice << "$parentStillRunning=Get-Process -Id " << juce::String(spec.processId) << " -ErrorAction SilentlyContinue; ";
        else
            notice << "$parentStillRunning=$false; ";
        notice << "if($verified -and -not $parentStillRunning){ "
               << desktopHostRelaunchCommand(spec.currentExePath) << " }; "
                  "} catch { Write-Warning ('Update failure notification/restart could not finish: '+$_.Exception.Message) }";
        script << powerShellCommand(notice);
    }
    script << "exit /b 1\r\n";
    if (spec.requireStartupAck)
        script << "# DIRECTPIPE_STARTUP_PS_BEGIN\r\n" << hostStartupPayload(spec)
               << "\r\n# DIRECTPIPE_STARTUP_PS_END\r\n";
    return script;
}

juce::String buildWindowsHostUpdateLauncherArguments(const juce::String& batchFilePath)
{
    if (!juce::File::isAbsolutePath(batchFilePath) || batchFilePath.containsAnyOf("\r\n\""))
        return {};
    // The BAT path is data in an encoded PowerShell command, then a private
    // child environment value. cmd expands the variable once, so literal
    // percent signs in caller/TEMP paths never become environment references.
    const juce::String command = "$ErrorActionPreference='Stop'; "
        "$start=New-Object Diagnostics.ProcessStartInfo; $start.FileName=$env:ComSpec; "
        "$start.Arguments='/d /v:off /c \"\"%DP_UPDATE_LAUNCH_FILE%\"\"'; "
        "$start.UseShellExecute=$false; $start.CreateNoWindow=$true; "
        "$start.EnvironmentVariables['DP_UPDATE_LAUNCH_FILE']='"
        + escapePowerShellQuote(batchFilePath) + "'; "
        "$child=[Diagnostics.Process]::Start($start); $child.WaitForExit(); exit $child.ExitCode";
    const auto utf16 = command.toUTF16();
    return "-NoProfile -NonInteractive -EncodedCommand "
        + juce::Base64::toBase64(utf16.getAddress(), utf16.sizeInBytes() - sizeof(juce::CharPointer_UTF16::CharType));
}

juce::File hostUpdateResultFileForInstall(const juce::File& userAppDataDirectory,
                                         const juce::String& executablePath)
{
    if (userAppDataDirectory.getFullPathName().isEmpty()
        || !juce::File::isAbsolutePath(executablePath))
        return {};

    auto identity = juce::File(executablePath).getFullPathName();
#if JUCE_WINDOWS
    identity = identity.replaceCharacter('/', '\\').toLowerCase();
#endif
    const auto key = juce::SHA256(identity.toRawUTF8(),
        static_cast<size_t>(identity.getNumBytesAsUTF8())).toHexString();
    return userAppDataDirectory.getChildFile("DirectPipe/update-results")
        .getChildFile(key + ".json");
}

HostUpdateStartupDecision classifyHostUpdateStartupReceipt(
    const juce::var& receipt, const juce::String& currentExePath,
    const juce::String& runningVersion)
{
    HostUpdateStartupDecision decision;
    if (!receipt.isObject())
        return decision;
    const auto receiptExe = receipt.getProperty("currentExePath", "").toString();
    if (!juce::File::isAbsolutePath(receiptExe)
        || !juce::File::isAbsolutePath(currentExePath))
        return decision;
    decision.matchesInstall = juce::File(receiptExe) == juce::File(currentExePath);
    if (!decision.matchesInstall)
        return decision;

    std::array<int, 3> expectedParts{}, runningParts{};
    juce::String expectedCanonical, runningCanonical;
    if (parseStrictReleaseVersion(receipt.getProperty("expectedVersion", "").toString(),
                                  expectedParts, expectedCanonical)
        && parseStrictReleaseVersion(runningVersion, runningParts, runningCanonical))
        decision.stale = expectedParts < runningParts;

    const auto successValue = receipt.getProperty("success", false);
    const bool success = successValue.isBool() && static_cast<bool>(successValue);
    const auto progressValue = receipt.getProperty("inProgress", false);
    const bool pending = !progressValue.isBool() || static_cast<bool>(progressValue);
    const bool restartFailed = receipt.getProperty("relaunchError", "").toString().trim().isNotEmpty();
    const auto acknowledgedValue = receipt.getProperty("attentionAcknowledged", false);
    const bool acknowledged = acknowledgedValue.isBool() && static_cast<bool>(acknowledgedValue);
    decision.retainRecoveryFiles = !decision.stale && (!success || pending || restartFailed);
    decision.showAttention = decision.retainRecoveryFiles && !acknowledged;
    return decision;
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
    if (!spec.isZip || spec.receiverTargets.empty() || spec.processWaitTimeoutMs <= 0
        || (!pathPreflightOnly && !validStartupPlan(spec))
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
    plan->setProperty("processWaitTimeoutMs", spec.processWaitTimeoutMs);
    plan->setProperty("resultFilePath", spec.resultFilePath);
    plan->setProperty("updatedFlagPath", spec.updatedFlagPath);
    plan->setProperty("relaunchHostAfterUpdate", spec.relaunchHostAfterUpdate);
    plan->setProperty("requireStartupAck", spec.requireStartupAck);
    plan->setProperty("startupAttemptId", spec.startupAttemptId);
    plan->setProperty("startupAckFilePath", spec.startupAckFilePath);
    plan->setProperty("startupAckTimeoutMs", spec.startupAckTimeoutMs);
    plan->setProperty("showInteractiveErrors", spec.showInteractiveErrors);
    plan->setProperty("logFilePath", spec.logFilePath);
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
    script << startupAcknowledgementFunctions();
    script << R"DIRECTPIPE(
$entries = New-Object 'System.Collections.Generic.List[object]'
$locks = @()
$archiveLock = $null
$archiveHashes = @{}
$work = $null
$result = @{ success = $false; error = ''; rollbackSucceeded = $true; installed = @(); hostUpdated = $false; relaunchError = ''; stage = 'preflight';
    filesCommitted = $false; inProgress = $false; startupReady = $false; backupCleanupSucceeded = $false;
    requireStartupAck = $plan.requireStartupAck; startupAttemptId = $plan.startupAttemptId; startupAckFilePath = $plan.startupAckFilePath;
    currentExePath = $plan.currentExePath; expectedVersion = $plan.expectedVersion; logFilePath = $plan.logFilePath }

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
        if ($plan.requireStartupAck) {
            $plan.startupAckFilePath = Assert-LiteralPath $plan.startupAckFilePath $false
            $null = Assert-LiteralPath ($plan.startupAckFilePath + '.tmp') $false
        }
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
        $result.stage = 'wait'
        $launchingProcess = Get-Process -Id $plan.processId -ErrorAction SilentlyContinue
        try {
            if ($launchingProcess -and -not $launchingProcess.WaitForExit($plan.processWaitTimeoutMs)) {
                throw 'Timed out waiting for the launching DirectPipe process. Close that instance and retry; no process was terminated.'
            }
        } finally { if ($launchingProcess) { $launchingProcess.Dispose() } }
    }
    $result.stage = 'prepare'
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
        if ($plan.requireStartupAck -and $entry.format -eq 'host') {
            $result.currentBeforeHash = Get-FileDigest $entry.target
            $result.backupBeforeHash = if ([IO.File]::Exists($entry.backup)) { Get-FileDigest $entry.backup } else { $null }
            $result.candidateHash = Get-FileDigest $entry.stage
        }
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
)DIRECTPIPE";
    script << R"DIRECTPIPE(    $result.installed = @($entries | ForEach-Object { $_.target })
    $result.hostUpdated = -not $plan.skipHostUpdate
    $result.filesCommitted = $true
    if ($plan.requireStartupAck) {
        foreach ($file in $locks) { $file.Dispose() }; $locks = @()
        $result.stage = 'startup-wait'; $result.inProgress = $true; $result.rollbackAttempted = $false
        Save-StartupReceipt $plan $result
        Start-UpdatedDesktopHost $plan
        Wait-VerifiedHostStartup $plan $result
        $result.stage = 'startup-cleanup'
        Save-StartupReceipt $plan $result
        $hostEntry = @($entries | Where-Object { $_.format -eq 'host' })[0]
        Remove-VerifiedHostBackups $result $hostEntry.backup $hostEntry.priorBackup
    }
    if (-not $plan.skipHostUpdate) {
        $null = Assert-LiteralPath $plan.updatedFlagPath $false
        [IO.File]::WriteAllText($plan.updatedFlagPath, $plan.expectedVersion, [Text.Encoding]::UTF8)
    }
    $result.success = $true
    $result.inProgress = $false
    $result.stage = 'complete'
    foreach ($file in $locks) { $file.Dispose() }; $locks = @()
    }
} catch {
    $result.error = $_.Exception.Message
    # Keep FILE_SHARE_DELETE handles through rollback: renames are allowed,
    # but another audio app must not load a half-installed module meanwhile.
    $result.inProgress = $false
    if ($plan.requireStartupAck -and $result.filesCommitted) {
        # A candidate may already be running. Startup failure must never
        # roll back any member of the committed host/Receiver pair beneath it.
        $result.relaunchError = if ($result.startupReady) { '' } else { $result.error }
        $result.rollbackAttempted = $false
        $result.nextCheck = 'New files are committed. Keep recovery files and check the startup error/update log; no automatic rollback was attempted.'
    } elseif (-not $plan.pathPreflightOnly) {
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
    if ($result.rollbackSucceeded) { $result.filesCommitted = $false; $result.hostUpdated = $false }
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
    } catch {
        if ($plan.requireStartupAck) { $result.success = $false; $result.error += '; cannot record update result: ' + $_.Exception.Message }
        Write-Error ('Cannot record update result: ' + $_.Exception.Message) -ErrorAction Continue
    }
}
if ($plan.requireStartupAck -and -not $plan.pathPreflightOnly -and -not $result.success -and -not $result.startupReady -and $plan.showInteractiveErrors) {
    try {
        Add-Type -AssemblyName System.Windows.Forms
        $message = 'DirectPipe update needs attention at ' + $result.stage + ': ' + $result.error
        if ($result.filesCommitted) { $message += "`nThe new files are committed. Keep retained recovery files; startup or cleanup was not fully confirmed." }
        else { $message += "`nCheck the recorded rollback status before starting DirectPipe." }
        $message += "`nLog: " + $plan.logFilePath + "`nResult: " + $plan.resultFilePath
        $null = [Windows.Forms.MessageBox]::Show($message, 'DirectPipe Update', 'OK', 'Warning')
    } catch { Write-Warning 'Could not display startup error; check the update log/result' }
}
$restartOriginalAfterRollback = $plan.requireStartupAck -and -not $result.filesCommitted -and -not $result.success -and $result.rollbackSucceeded
if ((-not $plan.requireStartupAck -or $restartOriginalAfterRollback) -and -not $plan.pathPreflightOnly -and $plan.relaunchHostAfterUpdate -and -not $plan.skipHostUpdate) {
    try {
        # Dispatch through the existing desktop shell instead of inheriting the
        # elevated updater token in a new audio host.
        if (-not $result.success -and -not $plan.requireStartupAck -and $plan.showInteractiveErrors) {
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
        # A restored original is not part of the new candidate's ACK attempt.
        $desktop.Document.Application.ShellExecute($plan.currentExePath, '', [IO.Path]::GetDirectoryName($plan.currentExePath), 'open', 1)
    } catch {
        $result.relaunchError = $_.Exception.Message
        try {
            if ($plan.showInteractiveErrors) {
                Add-Type -AssemblyName System.Windows.Forms
                $null = [Windows.Forms.MessageBox]::Show(('Start DirectPipe manually when ready: ' + $plan.currentExePath + "`n" + $result.relaunchError), 'DirectPipe Update', 'OK', 'Information')
            }
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
