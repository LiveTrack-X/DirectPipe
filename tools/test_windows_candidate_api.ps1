param(
    [Parameter(Mandatory = $true)][string]$Candidate,
    [string]$EvidenceDirectory = 'build/test-results'
)

$ErrorActionPreference = 'Stop'
$taskRepo = [IO.Path]::GetFullPath((Split-Path $PSScriptRoot -Parent))
$taskBuildRoot = (Join-Path $taskRepo 'build') + [IO.Path]::DirectorySeparatorChar
$taskCandidate = (Resolve-Path -LiteralPath $Candidate).Path
$taskEvidence = [IO.Path]::GetFullPath((Join-Path $taskRepo $EvidenceDirectory))
if (-not $taskCandidate.StartsWith($taskBuildRoot, [StringComparison]::OrdinalIgnoreCase) -or
    -not $taskEvidence.StartsWith($taskBuildRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Candidate and evidence must stay in this checkout build directory.'
}
$taskSource = Get-Content -LiteralPath (Join-Path $taskRepo 'CMakeLists.txt') -Raw
$taskMatch = [regex]::Match($taskSource, 'project\(DirectPipe VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $taskMatch.Success) { throw 'Cannot determine canonical source version.' }
$taskExpected = $taskMatch.Groups[1].Value
$taskInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($taskCandidate)
if ($taskInfo.ProductName -ne 'DirectPipe' -or $taskInfo.FileVersion -ne $taskExpected -or
    $taskInfo.ProductVersion -ne $taskExpected) { throw 'Candidate binary identity/version mismatch.' }
if (Get-NetTCPConnection -LocalPort 8765,8766 -State Listen -ErrorAction SilentlyContinue) {
    throw 'API port 8765 or 8766 is already owned. Refusing to test or stop another application.'
}
$taskDirectory = Split-Path $taskCandidate -Parent
if (Test-Path -LiteralPath (Join-Path $taskDirectory 'config')) {
    throw 'Candidate already has portable configuration; use a fresh isolated build.'
}
New-Item -ItemType Directory -Force -Path $taskEvidence | Out-Null
[IO.File]::WriteAllText((Join-Path $taskDirectory 'portable.flag'), '')
$taskConfigDirectory = Join-Path $taskDirectory 'config'
$taskSlotsDirectory = Join-Path $taskConfigDirectory 'slots'
$taskRecordingDirectory = Join-Path $taskEvidence 'recordings'
New-Item -ItemType Directory -Force -Path $taskSlotsDirectory,$taskRecordingDirectory | Out-Null
$taskChain = [ordered]@{ version=4; type='chain'; plugins=@(
    @{name='Filter';type='builtin_filter';bypassed=$false},
    @{name='Filter';type='builtin_filter';bypassed=$false}) }
foreach ($taskSlot in @('A','B','C','D','E','Auto')) {
    $taskSlotChain = if ($taskSlot -in @('C','E')) {
        [ordered]@{version=4;type='chain';plugins=@()}
    } else { $taskChain }
    $taskSlotChain | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskSlotsDirectory ("slot_$taskSlot.dppreset")) -Encoding utf8
}
$taskSettings = [ordered]@{ version=4; activeSlot=0; channelMode=2; monoInputEnabled=$false;
    recordingFolder=$taskRecordingDirectory; startMinimizedToTray=$false; plugins=$taskChain.plugins }
$taskSettings | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $taskConfigDirectory 'settings.dppreset') -Encoding utf8
@{recordingFolder=$taskRecordingDirectory} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskConfigDirectory 'recording-config.json') -Encoding utf8
$taskPreviousConfigHint = $env:DIRECTPIPE_TEST_CONFIG_DIR
$taskPreviousRecordingHint = $env:DIRECTPIPE_TEST_RECORDING_DIR
$env:DIRECTPIPE_TEST_CONFIG_DIR = $taskConfigDirectory
$env:DIRECTPIPE_TEST_RECORDING_DIR = $taskRecordingDirectory
$taskProcess = $null
$taskApiExit = -1
$taskGraceful = $false
$taskIdentity = [ordered]@{
    candidate = $taskCandidate; version = $taskExpected
    sha256 = (Get-FileHash -LiteralPath $taskCandidate -Algorithm SHA256).Hash.ToLowerInvariant()
    isolatedPortable = $true; apiExit = -1; gracefulShutdown = $false
}
Push-Location $taskRepo
try {
    $taskProcess = Start-Process -FilePath $taskCandidate -WorkingDirectory $taskDirectory -WindowStyle Hidden -PassThru
    $taskIdentity.pid = $taskProcess.Id
    $taskDeadline = [DateTime]::UtcNow.AddSeconds(60)
    $taskReady = $false
    while ([DateTime]::UtcNow -lt $taskDeadline) {
        if ($taskProcess.HasExited) { throw "Isolated candidate exited before API startup: $($taskProcess.ExitCode)" }
        $taskListeners = @(Get-NetTCPConnection -LocalPort 8765,8766 -State Listen -ErrorAction SilentlyContinue)
        if ($taskListeners.Count) {
            if ($taskListeners | Where-Object { $_.OwningProcess -ne $taskProcess.Id }) { throw 'HTTP/WebSocket listener belongs to another process.' }
        }
        if (@($taskListeners.LocalPort | Sort-Object -Unique).Count -eq 2) {
            try {
                $taskStatus = Invoke-WebRequest 'http://127.0.0.1:8766/api/status' -TimeoutSec 2 -UseBasicParsing
                if ($taskStatus.StatusCode -eq 200) { $taskReady = $true; break }
            } catch {}
        }
        Start-Sleep -Milliseconds 200
    }
    if (-not $taskReady) { throw 'Exact candidate API did not become ready within 60 seconds.' }
    $taskIdentity.httpAndWebSocketPidVerified = $true
    & bash tools/pre-release-test.sh --api-only 2>&1 | Tee-Object -FilePath (Join-Path $taskEvidence 'api.log')
    $taskApiExit = $LASTEXITCODE
    $taskIdentity.apiExit = $taskApiExit
    if ($taskApiExit -ne 0) { throw "Exact candidate API tests failed: $taskApiExit" }
} finally {
    if ($taskProcess -and -not $taskProcess.HasExited) {
        # Only the process created above is ever signalled or terminated.
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DirectPipeApiFixtureQuit {
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool PostThreadMessage(uint id, uint message, IntPtr wParam, IntPtr lParam);
}
'@
        $taskProcess.Refresh()
        [uint32]$taskWindowProcess = 0
        $taskThread = [DirectPipeApiFixtureQuit]::GetWindowThreadProcessId($taskProcess.MainWindowHandle, [ref]$taskWindowProcess)
        if (-not $taskThread) { $taskThread = ($taskProcess.Threads | Sort-Object StartTime | Select-Object -First 1).Id }
        if ($taskThread) {
            [DirectPipeApiFixtureQuit]::PostThreadMessage($taskThread, 0x0012, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            $taskGraceful = $taskProcess.WaitForExit(10000)
        }
        if (-not $taskProcess.HasExited) { $taskProcess.Kill(); $taskProcess.WaitForExit(10000) | Out-Null }
    }
    $taskIdentity.gracefulShutdown = $taskGraceful
    $taskIdentity | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskEvidence 'api-identity.json') -Encoding utf8
    foreach ($taskLogName in @('directpipe.log', 'directpipe.log.prev')) {
        $taskLog = Join-Path $taskDirectory ('config/' + $taskLogName)
        if (Test-Path -LiteralPath $taskLog) { Copy-Item -LiteralPath $taskLog -Destination (Join-Path $taskEvidence $taskLogName) }
    }
    Pop-Location
    $env:DIRECTPIPE_TEST_CONFIG_DIR = $taskPreviousConfigHint
    $env:DIRECTPIPE_TEST_RECORDING_DIR = $taskPreviousRecordingHint
}
if ($taskApiExit -ne 0) { exit 1 }
