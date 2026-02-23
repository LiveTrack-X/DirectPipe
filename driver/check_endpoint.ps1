# Check Virtual Loop Mic endpoint properties
$dev = Get-PnpDevice -FriendlyName '*Virtual Loop*' -Class AudioEndpoint -ErrorAction SilentlyContinue
if ($dev) {
    Write-Host "=== Endpoint Device Found ==="
    Write-Host "Status: $($dev.Status)"
    Write-Host "InstanceId: $($dev.InstanceId)"
    Write-Host ""

    # Get all properties
    $props = Get-PnpDeviceProperty -InstanceId $dev.InstanceId -ErrorAction SilentlyContinue
    $props | Where-Object { $_.KeyName -like '*Volume*' -or $_.KeyName -like '*Format*' -or $_.KeyName -like '*Default*' } | Format-Table KeyName, Type, Data -AutoSize
} else {
    Write-Host "Virtual Loop Mic endpoint NOT found"
}

# Check the driver EP\0 registry for volume property
Write-Host ""
Write-Host "=== Checking driver registry EP\0 ==="
$drvDev = Get-PnpDevice -FriendlyName 'Virtual Loop Mic' -Class MEDIA -ErrorAction SilentlyContinue
if ($drvDev) {
    $regPath = "HKLM:\SYSTEM\CurrentControlSet\Enum\$($drvDev.InstanceId)\Device Parameters\EP\0"
    if (Test-Path $regPath) {
        Write-Host "EP\0 registry path exists: $regPath"
        Get-ItemProperty -Path $regPath | Format-List
    } else {
        Write-Host "EP\0 registry path NOT found: $regPath"
        # Try alternate paths
        $basePath = "HKLM:\SYSTEM\CurrentControlSet\Enum\$($drvDev.InstanceId)\Device Parameters"
        if (Test-Path $basePath) {
            Write-Host "Device Parameters exists. Subkeys:"
            Get-ChildItem -Path $basePath -Recurse | ForEach-Object { Write-Host $_.PSPath }
        }
    }
}
