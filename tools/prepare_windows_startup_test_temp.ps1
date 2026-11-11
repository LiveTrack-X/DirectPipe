# SPDX-License-Identifier: GPL-3.0-or-later
# CI harness only. Keep strict probe containment and production launch code intact.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows') {
    throw 'Startup-test TEMP alignment is restricted to the Windows Actions runner.'
}

if ($null -eq ('DirectPipeStartupTestTempAnchor' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
public static class DirectPipeStartupTestTempAnchor {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
    private static extern uint GetTempPathW(uint length, StringBuilder buffer);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
    private static extern SafeFileHandle CreateFileW(string path, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
    private static extern uint GetFinalPathNameByHandleW(SafeFileHandle handle,
        StringBuilder buffer, uint length, uint flags);
    [DllImport("shell32.dll", ExactSpelling=true)]
    private static extern int SHGetKnownFolderPath(ref Guid folder, uint flags,
        IntPtr token, out IntPtr path);
    public static string NativeTemp() {
        var buffer = new StringBuilder(32768);
        uint length = GetTempPathW((uint)buffer.Capacity, buffer);
        if (length == 0 || length >= buffer.Capacity) throw new Win32Exception();
        return buffer.ToString();
    }
    public static string LocalAppData() {
        var folder = new Guid("F1B32785-6FBA-4FCF-9D55-7B8E7F157091");
        IntPtr path = IntPtr.Zero;
        try {
            int result = SHGetKnownFolderPath(ref folder, 0, IntPtr.Zero, out path);
            Marshal.ThrowExceptionForHR(result);
            return Marshal.PtrToStringUni(path);
        } finally {
            if (path != IntPtr.Zero) Marshal.FreeCoTaskMem(path);
        }
    }
    public static string CanonicalDirectory(string path) {
        using (var handle = CreateFileW(path, 0x80, 7, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero)) {
            if (handle.IsInvalid) throw new Win32Exception();
            var buffer = new StringBuilder(32768);
            uint length = GetFinalPathNameByHandleW(handle, buffer, (uint)buffer.Capacity, 0);
            if (length == 0 || length >= buffer.Capacity) throw new Win32Exception();
            return buffer.ToString().TrimEnd('\\');
        }
    }
}
'@
}

function Get-StartupTempLogText([string] $Value) {
    if ($null -eq $Value) { return '' }
    if ($Value.Length -gt 4096) { return $Value.Substring(0, 4096) }
    return $Value
}

$originalTemp = $env:TEMP
$originalTmp = $env:TMP
$process = [Diagnostics.Process]::GetCurrentProcess()
try { $sessionId = $process.SessionId } finally { $process.Dispose() }
$record = [ordered]@{ aligned = $false; sessionId = $sessionId; reason = 'not-selected' }
try {
    $before = [DirectPipeStartupTestTempAnchor]::NativeTemp()
    $record.beforeNativeTemp = Get-StartupTempLogText $before
    $profileTemp = [IO.Path]::Combine([DirectPipeStartupTestTempAnchor]::LocalAppData(), 'Temp')
    $sessionTemp = [IO.Path]::Combine($profileTemp,
        $sessionId.ToString([Globalization.CultureInfo]::InvariantCulture))
    $record.profileTemp = Get-StartupTempLogText $profileTemp
    $record.sessionTemp = Get-StartupTempLogText $sessionTemp

    if (-not [IO.Directory]::Exists($profileTemp)) {
        $record.reason = 'profile-temp-unavailable'
    } elseif (-not [IO.Directory]::Exists($sessionTemp)) {
        $record.reason = 'current-session-temp-unavailable'
    } else {
        $beforeIdentity = [DirectPipeStartupTestTempAnchor]::CanonicalDirectory($before)
        $profileIdentity = [DirectPipeStartupTestTempAnchor]::CanonicalDirectory($profileTemp)
        if (-not [string]::Equals($beforeIdentity, $profileIdentity, [StringComparison]::OrdinalIgnoreCase)) {
            # Preserve arbitrary custom/scoped TEMP, including prefix-boundary
            # regression fixtures; never search for or guess another directory.
            $record.reason = 'native-temp-is-not-profile-base'
        } else {
            $sessionIdentity = [DirectPipeStartupTestTempAnchor]::CanonicalDirectory($sessionTemp)
            [Environment]::SetEnvironmentVariable('TEMP', $sessionTemp, 'Process')
            [Environment]::SetEnvironmentVariable('TMP', $sessionTemp, 'Process')
            $afterIdentity = [DirectPipeStartupTestTempAnchor]::CanonicalDirectory(
                [DirectPipeStartupTestTempAnchor]::NativeTemp())
            if (-not [string]::Equals($afterIdentity, $sessionIdentity, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'Selected startup-test TEMP did not resolve to the existing session directory.'
            }
            $record.aligned = $true
            $record.reason = 'existing-current-session-temp'
        }
    }
    $record.afterNativeTemp = Get-StartupTempLogText ([DirectPipeStartupTestTempAnchor]::NativeTemp())
    $record | ConvertTo-Json -Compress
} catch {
    [Environment]::SetEnvironmentVariable('TEMP', $originalTemp, 'Process')
    [Environment]::SetEnvironmentVariable('TMP', $originalTmp, 'Process')
    $record.aligned = $false
    $record.reason = 'alignment-failed-parent-environment-restored'
    $record.hresult = $_.Exception.HResult
    $record | ConvertTo-Json -Compress
    throw
}
