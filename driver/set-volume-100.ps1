# Set Virtual Loop Mic volume to 100% via SYSTEM scheduled task
# Must run as Administrator
# The MMDevices registry is protected by SYSTEM-only ACL,
# so we use a scheduled task running as SYSTEM to modify it.

$taskName = "DirectPipe_SetMicVol"
$scriptBlock = @'
$base = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture'
Get-ChildItem $base -ErrorAction SilentlyContinue | ForEach-Object {
    $propsPath = Join-Path $_.PSPath 'Properties'
    if (Test-Path $propsPath) {
        $p = Get-ItemProperty $propsPath -ErrorAction SilentlyContinue
        $name = $p.'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
        if ($name -like '*Virtual Loop*') {
            # Set master volume to 1.0 (100%)
            # Property key {219ED5A0-9CBF-4F3A-B927-637C29CF7F0E},0 = endpoint master volume (float, 0.0-1.0)
            # This is stored as a REG_BINARY float value
            $volBytes = [BitConverter]::GetBytes([float]1.0)
            Set-ItemProperty -Path $propsPath -Name '{219ed5a0-9cbf-4f3a-b927-637c29cf7f0e},0' -Value $volBytes -Type Binary -ErrorAction SilentlyContinue

            # Also set the level via the scalar volume
            # {3ce13f2b-87e5-4d2e-8c5f-72094f2c8511},0 = volume level
            Set-ItemProperty -Path $propsPath -Name '{3ce13f2b-87e5-4d2e-8c5f-72094f2c8511},0' -Value $volBytes -Type Binary -ErrorAction SilentlyContinue

            Write-Output "Set Virtual Loop Mic volume to 100%"
        }
    }
}
'@

# Write script to temp
$tempScript = "$env:TEMP\directpipe_setvol.ps1"
$scriptBlock | Out-File -FilePath $tempScript -Encoding UTF8

# Create and run scheduled task as SYSTEM
$action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$tempScript`""
$principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
Register-ScheduledTask -TaskName $taskName -Action $action -Principal $principal -Force | Out-Null
Start-ScheduledTask -TaskName $taskName

# Wait for completion
Start-Sleep -Seconds 2
$result = Get-ScheduledTaskInfo -TaskName $taskName -ErrorAction SilentlyContinue
Write-Host "Task result: $($result.LastTaskResult)"

# Cleanup
Unregister-ScheduledTask -TaskName $taskName -Confirm:$false -ErrorAction SilentlyContinue
Remove-Item $tempScript -Force -ErrorAction SilentlyContinue

Write-Host "Done. Restart audio services to apply."
