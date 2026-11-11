# SPDX-License-Identifier: GPL-3.0-or-later
# CI-only diagnostic. Never launches DirectPipe or opens an audio device.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows' -or
    [string]::IsNullOrWhiteSpace($env:RUNNER_TEMP)) {
    throw 'This diagnostic is restricted to the Windows Actions runner.'
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$nonce = [Guid]::NewGuid().ToString('N')
$workDir = Join-Path $env:RUNNER_TEMP ('directpipe-shell-context-' + $nonce)
$evidenceDir = Join-Path $repoRoot ('build/test-results/windows-shell-context-' + $nonce)
foreach ($dir in @($workDir, $evidenceDir)) {
    if (Test-Path -LiteralPath $dir) { throw 'Diagnostic directory already exists.' }
    New-Item -ItemType Directory -Path $dir | Out-Null
}
$scopedTemp = Join-Path $workDir 'parent-temp'
New-Item -ItemType Directory -Path $scopedTemp | Out-Null
$probePath = Join-Path $workDir 'DirectPipeShellContextProbe.exe'
$sourcePath = Join-Path $workDir 'DirectPipeShellContextProbe.cs'
$utf8 = New-Object System.Text.UTF8Encoding($false)

$probeSource = @'
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Security.Principal;
using System.Text;
using System.Web.Script.Serialization;

internal static class Program
{
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
    private static extern uint GetTempPathW(uint length, StringBuilder buffer);

    private static string Bounded(string value)
    {
        value = value ?? "";
        return value.Length <= 4096 ? value : value.Substring(0, 4096);
    }

    private static string NativeTempPath()
    {
        var buffer = new StringBuilder(32768);
        uint length = GetTempPathW((uint)buffer.Capacity, buffer);
        return length > 0 && length < buffer.Capacity
            ? Bounded(buffer.ToString())
            : "GetTempPathW error " + Marshal.GetLastWin32Error();
    }

    [STAThread]
    private static int Main(string[] args)
    {
        Guid nonce;
        if (args.Length != 2 || (args[0] != "direct" && args[0] != "shell")
            || !Guid.TryParseExact(args[1], "N", out nonce) || nonce == Guid.Empty
            || !String.Equals(nonce.ToString("N"), args[1], StringComparison.Ordinal))
            return 2;

        try
        {
            using (var process = Process.GetCurrentProcess())
            using (var identity = WindowsIdentity.GetCurrent())
            {
                string modulePath = Path.GetFullPath(process.MainModule.FileName);
                // The caller cannot supply an output path. GUID+fixed mode are
                // the only filename components, beside this executable.
                string output = Path.Combine(Path.GetDirectoryName(modulePath),
                    "shell-context." + args[1] + "." + args[0] + ".json");
                var record = new Dictionary<string, object> {
                    { "version", 1 }, { "nonce", args[1] }, { "mode", args[0] },
                    { "pid", process.Id },
                    { "sid", identity.User == null ? "" : Bounded(identity.User.Value) },
                    { "name", Bounded(identity.Name) },
                    { "sessionId", process.SessionId },
                    { "modulePath", Bounded(modulePath) },
                    { "tempEnv", Bounded(Environment.GetEnvironmentVariable("TEMP")) },
                    { "tmpEnv", Bounded(Environment.GetEnvironmentVariable("TMP")) },
                    { "nativeTempPath", NativeTempPath() },
                    { "dotNetTempPath", Bounded(Path.GetTempPath()) }
                };
                byte[] json = new UTF8Encoding(false).GetBytes(new JavaScriptSerializer().Serialize(record));
                using (var stream = new FileStream(output, FileMode.CreateNew,
                                                   FileAccess.Write, FileShare.Read))
                {
                    stream.Write(json, 0, json.Length);
                    stream.Flush(true);
                }
            }
            return 0;
        }
        catch { return 3; }
    }
}
'@
[IO.File]::WriteAllText($sourcePath, $probeSource, $utf8)
$compiler = Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf)) {
    throw 'The built-in .NET Framework C# compiler is missing.'
}
& $compiler /nologo /target:winexe /platform:x64 /optimize+ /r:System.Web.Extensions.dll "/out:$probePath" $sourcePath `
    2>&1 | Out-File -LiteralPath (Join-Path $evidenceDir 'compile.log') -Encoding utf8
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $probePath -PathType Leaf)) {
    throw 'The bounded shell-context probe did not compile.'
}

Add-Type -TypeDefinition @'
using System.Runtime.InteropServices;
using System.Text;
public static class DirectPipeShellContextNative {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true, SetLastError = true)]
    public static extern uint GetTempPathW(uint length, StringBuilder buffer);
}
'@

function Get-BoundedText([string] $Value) {
    if ($null -eq $Value) { return '' }
    if ($Value.Length -gt 4096) { return $Value.Substring(0, 4096) }
    return $Value
}

function Get-CallerContext {
    $process = [Diagnostics.Process]::GetCurrentProcess()
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try {
        $buffer = New-Object Text.StringBuilder(32768)
        $length = [DirectPipeShellContextNative]::GetTempPathW($buffer.Capacity, $buffer)
        $nativeTemp = if ($length -gt 0 -and $length -lt $buffer.Capacity) {
            $buffer.ToString()
        } else { 'GetTempPathW error ' + [Runtime.InteropServices.Marshal]::GetLastWin32Error() }
        return [ordered]@{
            pid = $process.Id
            sid = Get-BoundedText $identity.User.Value
            name = Get-BoundedText $identity.Name
            sessionId = $process.SessionId
            modulePath = Get-BoundedText $process.MainModule.FileName
            tempEnv = Get-BoundedText $env:TEMP
            tmpEnv = Get-BoundedText $env:TMP
            nativeTempPath = Get-BoundedText $nativeTemp
            dotNetTempPath = Get-BoundedText ([IO.Path]::GetTempPath())
        }
    } finally {
        $identity.Dispose()
        $process.Dispose()
    }
}

function Wait-ProbeReceipt([string] $Mode) {
    $receipt = Join-Path $workDir ('shell-context.' + $nonce + '.' + $Mode + '.json')
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $sawReceipt = $false
    while ($timer.ElapsedMilliseconds -lt 30000) {
        if ([IO.File]::Exists($receipt)) {
            $sawReceipt = $true
            try {
                if ((Get-Item -LiteralPath $receipt).Length -gt 65536) { break }
                $record = [IO.File]::ReadAllText($receipt, [Text.Encoding]::UTF8) | ConvertFrom-Json
                if ($record.version -eq 1 -and $record.nonce -ceq $nonce -and
                    $record.mode -ceq $Mode -and $record.pid -gt 0) {
                    [IO.File]::Copy($receipt, (Join-Path $evidenceDir ('child-' + $Mode + '.json')), $false)
                    return [ordered]@{ status = 'receipt'; context = $record }
                }
            } catch { } # A CreateNew receipt may still be completing its write.
        }
        Start-Sleep -Milliseconds 100
    }
    return [ordered]@{ status = $(if ($sawReceipt) { 'unreadable-or-invalid-receipt' } else { 'no-receipt' }) }
}

function Invoke-DesktopShellProbe([string] $Executable, [string] $Arguments, [string] $Directory) {
    $application = New-Object -ComObject Shell.Application
    try {
        $windows = $application.Windows()
        $location = 0; $desktopRoot = 0; $hwnd = 0
        $desktop = $windows.FindWindowSW([ref]$location, [ref]$desktopRoot, 8, [ref]$hwnd, 1)
        if (-not $desktop -or -not $desktop.Document.Application) {
            throw 'The original desktop shell is unavailable.'
        }
        # Use the updater's exact desktop resolution route. The diagnostic
        # alone requests SW_HIDE (0); its GUI probes create no application UI.
        [void]$desktop.Document.Application.ShellExecute($Executable, $Arguments, $Directory, 'open', 0)
    } finally {
        [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($application)
    }
}

$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$summary = [ordered]@{ version = 1; nonce = $nonce; callerBefore = Get-CallerContext }
try {
    [Environment]::SetEnvironmentVariable('TEMP', $scopedTemp, 'Process')
    [Environment]::SetEnvironmentVariable('TMP', $scopedTemp, 'Process')
    $summary.callerScoped = Get-CallerContext

    try {
        $direct = Start-Process -FilePath $probePath -ArgumentList @('direct', $nonce) `
            -WorkingDirectory $workDir -WindowStyle Hidden -PassThru
        try { $summary.direct = Wait-ProbeReceipt 'direct' } finally { $direct.Dispose() }
    } catch {
        $summary.direct = [ordered]@{ status = 'launch-error'; hresult = $_.Exception.HResult }
    }

    try {
        Invoke-DesktopShellProbe $probePath ('shell ' + $nonce) $workDir
        $summary.shell = Wait-ProbeReceipt 'shell'
    } catch {
        $summary.shell = [ordered]@{ status = 'launch-error'; hresult = $_.Exception.HResult }
    }
} finally {
    [Environment]::SetEnvironmentVariable('TEMP', $oldTemp, 'Process')
    [Environment]::SetEnvironmentVariable('TMP', $oldTmp, 'Process')
    $summary.callerRestored = Get-CallerContext
    [IO.File]::WriteAllText((Join-Path $evidenceDir 'context.json'),
        ($summary | ConvertTo-Json -Depth 8), $utf8)
}

function Wait-NativeProbe([string] $Directory, [Diagnostics.Process] $DirectProcess) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $diagnostic = Join-Path $Directory 'startup-probe.diagnostic.txt'
    $ack = Join-Path $Directory ('native.' + $nonce + '.startup.json')
    while ($timer.ElapsedMilliseconds -lt 30000) {
        if ([IO.File]::Exists($diagnostic) -and (Get-Item -LiteralPath $diagnostic).Length -gt 0) { break }
        if ($null -ne $DirectProcess -and $DirectProcess.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    $result = [ordered]@{ status = 'no-ack'; pid = $null; ack = $null; diagnostic = '' }
    if ($null -ne $DirectProcess) { $result.directLaunchPid = $DirectProcess.Id }
    $pidFile = Join-Path $Directory 'startup-probe.pid'
    if ([IO.File]::Exists($pidFile) -and (Get-Item -LiteralPath $pidFile).Length -le 64) {
        $pidText = [IO.File]::ReadAllText($pidFile).Trim()
        if ($pidText -match '^[0-9]{1,10}$') { $result.pid = [long]$pidText }
    }
    if ([IO.File]::Exists($diagnostic) -and (Get-Item -LiteralPath $diagnostic).Length -le 4096) {
        $result.diagnostic = [IO.File]::ReadAllText($diagnostic)
    }
    if ([IO.File]::Exists($ack) -and (Get-Item -LiteralPath $ack).Length -le 16384) {
        try {
            $record = [IO.File]::ReadAllText($ack, [Text.Encoding]::UTF8) | ConvertFrom-Json
            if ($record.attemptId -ceq $nonce -and $record.pid -gt 0 -and $record.ready -eq $true) {
                $result.ack = $record
                $result.status = 'ack'
            }
        } catch { $result.status = 'invalid-ack' }
    }
    if ($null -ne $DirectProcess -and $DirectProcess.HasExited) {
        $result.directExitCode = $DirectProcess.ExitCode
    }
    return $result
}

# Restore the parent TEMP/TMP before these controls: their directories must be
# under the caller's real native temp, not the artificial C# inheritance trial.
$nativeOriginalTemp = $env:TEMP
$nativeOriginalTmp = $env:TMP
try {
& (Join-Path $PSScriptRoot 'prepare_windows_startup_test_temp.ps1') |
    Tee-Object -FilePath (Join-Path $evidenceDir 'temp-alignment.json')
$summary.nativeCaller = Get-CallerContext
$summary.native = [ordered]@{}
$nativeTemp = $summary.nativeCaller.nativeTempPath
if (-not [IO.Directory]::Exists($nativeTemp)) { throw 'Caller native TEMP is unavailable.' }
$nativeSource = Join-Path $repoRoot 'tests/update_startup_probe.cpp'
$nativeReady = $true
foreach ($runtime in @('MD', 'MT')) {
    $runtimeResult = [ordered]@{}
    $summary.native[$runtime] = $runtimeResult
    $buildDir = Join-Path $nativeTemp ('directpipe-native-' + $nonce + '-' + $runtime)
    if (Test-Path -LiteralPath $buildDir) { throw 'Native diagnostic directory already exists.' }
    New-Item -ItemType Directory -Path $buildDir | Out-Null
    $nativeExe = Join-Path $buildDir 'DirectPipeShellNativeProbe.exe'
    $compileLog = Join-Path $evidenceDir ('native-' + $runtime + '-compile.log')
    try {
        $compileArguments = @('/nologo', '/std:c++17', '/EHsc', '/O2', ('/' + $runtime),
            '/DUNICODE', '/D_UNICODE', '/DNOMINMAX',
            ('/Fo' + (Join-Path $buildDir 'probe.obj')), ('/Fe' + $nativeExe),
            $nativeSource, '/link', '/SUBSYSTEM:WINDOWS', 'shell32.lib')
        & cl.exe @compileArguments 2>&1 | Out-File -LiteralPath $compileLog -Encoding utf8
        if ($LASTEXITCODE -ne 0 -or -not [IO.File]::Exists($nativeExe)) {
            throw 'Native probe compilation failed.'
        }
        & dumpbin.exe /dependents $nativeExe 2>&1 |
            Out-File -LiteralPath (Join-Path $evidenceDir ('native-' + $runtime + '-dependencies.log')) -Encoding utf8
        $runtimeResult.sha256 = (Get-FileHash -LiteralPath $nativeExe -Algorithm SHA256).Hash.ToLowerInvariant()
    } catch {
        $runtimeResult.status = 'compile-error'
        $runtimeResult.hresult = $_.Exception.HResult
        $nativeReady = $false
        continue
    }

    foreach ($mode in @('direct', 'shell')) {
        $caseDir = Join-Path $buildDir $mode
        New-Item -ItemType Directory -Path $caseDir | Out-Null
        $caseExe = Join-Path $caseDir 'DirectPipeShellNativeProbe.exe'
        [IO.File]::Copy($nativeExe, $caseExe, $false)
        $ack = Join-Path $caseDir ('native.' + $nonce + '.startup.json')
        $ini = "[StartupProbe]`r`nAckPath=$ack`r`nMode=ready`r`nVersion=4.5.0`r`nLifetimeMs=1000`r`n"
        [IO.File]::WriteAllText((Join-Path $caseDir 'startup-probe.ini'), $ini, [Text.Encoding]::Unicode)
        $direct = $null
        try {
            $argument = '--directpipe-update-attempt=' + $nonce
            if ($mode -eq 'direct') {
                $direct = Start-Process -FilePath $caseExe -ArgumentList $argument `
                    -WorkingDirectory $caseDir -WindowStyle Hidden -PassThru
            } else {
                Invoke-DesktopShellProbe $caseExe $argument $caseDir
            }
            $runtimeResult[$mode] = Wait-NativeProbe $caseDir $direct
        } catch {
            $runtimeResult[$mode] = [ordered]@{ status = 'launch-error'; hresult = $_.Exception.HResult }
        } finally {
            if ($null -ne $direct) { $direct.Dispose() }
        }
        if ($runtimeResult[$mode].status -ne 'ack') { $nativeReady = $false }
    }
}
} finally {
    [Environment]::SetEnvironmentVariable('TEMP', $nativeOriginalTemp, 'Process')
    [Environment]::SetEnvironmentVariable('TMP', $nativeOriginalTmp, 'Process')
    $summary.nativeCallerRestored = Get-CallerContext
}
[IO.File]::WriteAllText((Join-Path $evidenceDir 'context.json'),
    ($summary | ConvertTo-Json -Depth 10), $utf8)
# Report only the bounded context and owned probe outcomes, never PATH or the
# environment as a whole. Dependency logs list imported DLL names, not settings.
$summary | ConvertTo-Json -Depth 10
if ($summary.direct.status -ne 'receipt' -or $summary.shell.status -ne 'receipt' -or -not $nativeReady) {
    exit 1
}
