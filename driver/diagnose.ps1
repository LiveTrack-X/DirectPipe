# Diagnose Virtual Loop Mic driver issues
Write-Host "=== 1. Driver file timestamp ===" -ForegroundColor Cyan
$sysFile = "C:\Users\livet\Desktop\DirectLoopMic\driver\out\virtualloop.sys"
$infFile = "C:\Users\livet\Desktop\DirectLoopMic\driver\out\virtualloop.inf"
if (Test-Path $sysFile) {
    $fi = Get-Item $sysFile
    Write-Host "  virtualloop.sys: $($fi.LastWriteTime) ($($fi.Length) bytes)"
} else {
    Write-Host "  virtualloop.sys NOT FOUND" -ForegroundColor Red
}
if (Test-Path $infFile) {
    $fi = Get-Item $infFile
    Write-Host "  virtualloop.inf: $($fi.LastWriteTime) ($($fi.Length) bytes)"
} else {
    Write-Host "  virtualloop.inf NOT FOUND" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== 2. PnP device status ===" -ForegroundColor Cyan
$mediaDev = Get-PnpDevice -FriendlyName 'Virtual Loop Mic' -Class MEDIA -ErrorAction SilentlyContinue
if ($mediaDev) {
    Write-Host "  MEDIA device: Status=$($mediaDev.Status), InstanceId=$($mediaDev.InstanceId)"
} else {
    Write-Host "  MEDIA device NOT FOUND" -ForegroundColor Red
}
$epDev = Get-PnpDevice -FriendlyName '*Virtual Loop*' -Class AudioEndpoint -ErrorAction SilentlyContinue
if ($epDev) {
    Write-Host "  AudioEndpoint: Status=$($epDev.Status), InstanceId=$($epDev.InstanceId)"
} else {
    Write-Host "  AudioEndpoint NOT FOUND" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== 3. Device interface registry (KSCATEGORY_AUDIO) ===" -ForegroundColor Cyan
# Find the device interface key for our capture filter
$audioGuid = "{6994AD04-93EF-11D0-A3CC-00A0C9223196}"
$captureGuid = "{65E8773D-8F56-11D0-A3B9-00A0C9223196}"
$ifacePath = "HKLM:\SYSTEM\CurrentControlSet\Control\DeviceClasses\$audioGuid"
if (Test-Path $ifacePath) {
    $subkeys = Get-ChildItem $ifacePath -ErrorAction SilentlyContinue
    foreach ($sk in $subkeys) {
        if ($sk.Name -like '*VirtualLoop*') {
            Write-Host "  Found interface key: $($sk.PSChildName)"
            # Check for EP\0
            $epPath = Join-Path $sk.PSPath '##?#ROOT#VirtualLoopMic#*\Device Parameters\EP\0' -ErrorAction SilentlyContinue
            # Try to enumerate subkeys
            $refKeys = Get-ChildItem $sk.PSPath -Recurse -ErrorAction SilentlyContinue
            foreach ($rk in $refKeys) {
                if ($rk.PSChildName -eq '0' -and $rk.PSPath -like '*EP*') {
                    Write-Host "  EP\0 found at: $($rk.PSPath)" -ForegroundColor Green
                    $epProps = Get-ItemProperty $rk.PSPath -ErrorAction SilentlyContinue
                    $epProps.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } | ForEach-Object {
                        Write-Host "    $($_.Name) = $($_.Value)"
                    }
                }
            }
        }
    }
} else {
    Write-Host "  KSCATEGORY_AUDIO path not found" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== 4. Device interface registry (KSCATEGORY_CAPTURE) ===" -ForegroundColor Cyan
$ifacePath2 = "HKLM:\SYSTEM\CurrentControlSet\Control\DeviceClasses\$captureGuid"
if (Test-Path $ifacePath2) {
    $subkeys2 = Get-ChildItem $ifacePath2 -ErrorAction SilentlyContinue
    foreach ($sk in $subkeys2) {
        if ($sk.Name -like '*VirtualLoop*') {
            Write-Host "  Found interface key: $($sk.PSChildName)"
            $refKeys = Get-ChildItem $sk.PSPath -Recurse -ErrorAction SilentlyContinue
            foreach ($rk in $refKeys) {
                if ($rk.PSChildName -eq '0' -and $rk.PSPath -like '*EP*') {
                    Write-Host "  EP\0 found at: $($rk.PSPath)" -ForegroundColor Green
                    $epProps = Get-ItemProperty $rk.PSPath -ErrorAction SilentlyContinue
                    $epProps.PSObject.Properties | Where-Object { $_.Name -notlike 'PS*' } | ForEach-Object {
                        Write-Host "    $($_.Name) = $($_.Value)"
                    }
                }
            }
        }
    }
} else {
    Write-Host "  KSCATEGORY_CAPTURE path not found" -ForegroundColor Red
}

Write-Host ""
Write-Host "=== 5. MMDevices endpoint cache ===" -ForegroundColor Cyan
$mmCapture = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture"
if (Test-Path $mmCapture) {
    Get-ChildItem $mmCapture -ErrorAction SilentlyContinue | ForEach-Object {
        $propsPath = Join-Path $_.PSPath 'Properties'
        if (Test-Path $propsPath) {
            $p = Get-ItemProperty $propsPath -ErrorAction SilentlyContinue
            $name = $p.'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'
            if ($name -like '*Virtual*' -or $name -like '*Loop*') {
                Write-Host "  Found: $name" -ForegroundColor Green
                Write-Host "  GUID: $($_.PSChildName)"
                # Check volume property  {1da5d803-d492-4edd-8c23-e0c0ffee7f0e},9
                $vol = $p.'{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},9'
                Write-Host "  Default Volume: $vol"
                # Check current level {3ce13f2b-87e5-4d2e-8c5f-72094f2c8511},0
                # This is the master volume level
            }
        }
    }
}

Write-Host ""
Write-Host "=== 6. OEM drivers with virtualloop ===" -ForegroundColor Cyan
$pnpOut = pnputil /enum-drivers 2>&1
$lines = $pnpOut -split "`n"
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'virtualloop') {
        # Print surrounding context
        $start = [Math]::Max(0, $i - 3)
        $end = [Math]::Min($lines.Count - 1, $i + 3)
        for ($j = $start; $j -le $end; $j++) {
            Write-Host "  $($lines[$j])"
        }
    }
}

Write-Host ""
Write-Host "=== Done ===" -ForegroundColor Cyan
