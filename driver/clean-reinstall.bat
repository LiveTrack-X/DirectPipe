@echo off
REM Clean reinstall: remove device, clear endpoint cache, reinstall
REM Must run as Administrator
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: Run as Administrator
    pause
    exit /b 1
)

set "DEVCON=C:\Program Files (x86)\Windows Kits\10\Tools\10.0.26100.0\x64\devcon.exe"
set "OUT_DIR=%~dp0out"

echo.
echo === Step 1: Stop audio services ===
net stop Audiosrv >nul 2>&1
net stop AudioEndpointBuilder >nul 2>&1

echo === Step 2: Remove device ===
"%DEVCON%" remove ROOT\VirtualLoopMic >nul 2>&1

echo === Step 3: Remove ALL OEM drivers for VirtualLoopMic ===
REM Use pnputil with English-safe parsing
pnputil /enum-drivers /class "MEDIA" > "%TEMP%\pnp_enum.txt" 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$lines = Get-Content '%TEMP%\pnp_enum.txt'; for ($i=0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match 'virtualloop') { for ($j = [Math]::Max(0,$i-5); $j -le $i; $j++) { if ($lines[$j] -match '(oem\d+\.inf)') { Write-Host ('Removing: ' + $Matches[1]); pnputil /delete-driver $Matches[1] /uninstall /force 2>&1 | Out-Null } } } }"

echo === Step 4: Clear cached audio endpoint (SYSTEM permissions) ===
REM Take ownership and delete the Virtual Loop Mic endpoint from MMDevices
REM First find the endpoint GUID
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$base = 'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Capture'; Get-ChildItem $base -EA SilentlyContinue | ForEach-Object { $pp = Join-Path $_.PSPath 'Properties'; if (Test-Path $pp) { try { $nm = (Get-ItemProperty $pp -EA Stop).'{b3f8fa53-0004-438e-9003-51a46e139bfc},6'; if ($nm -like '*Virtual Loop*') { Write-Host ('Found endpoint: ' + $_.PSChildName + ' = ' + $nm); $regPath = $_.Name -replace '^HKEY_LOCAL_MACHINE','HKLM:'; Write-Host ('Taking ownership...'); $acl = Get-Acl $regPath; $admin = [System.Security.Principal.NTAccount]'BUILTIN\Administrators'; $acl.SetOwner($admin); $rule = New-Object System.Security.AccessControl.RegistryAccessRule($admin,'FullControl','ContainerInherit,ObjectInherit','None','Allow'); $acl.AddAccessRule($rule); Set-Acl $regPath $acl; Get-ChildItem $_.PSPath -Recurse -EA SilentlyContinue | ForEach-Object { try { $ca = Get-Acl $_.PSPath; $ca.SetOwner($admin); $ca.AddAccessRule($rule); Set-Acl $_.PSPath $ca } catch {} }; Remove-Item $_.PSPath -Recurse -Force -EA SilentlyContinue; if (-not (Test-Path $_.PSPath)) { Write-Host 'Endpoint cache cleared!' -ForegroundColor Green } else { Write-Host 'Failed to delete (trying reg.exe)...' -ForegroundColor Yellow; $rawKey = $_.Name; cmd /c \"reg delete `\"$rawKey`\" /f\" 2>&1 | Out-Null; if (-not (Test-Path $_.PSPath)) { Write-Host 'Cleared via reg.exe!' -ForegroundColor Green } else { Write-Host 'Still failed. Will continue anyway.' -ForegroundColor Red } } } } catch {} } }"

timeout /t 2 /nobreak >nul

echo === Step 5: Install fresh ===
pnputil /add-driver "%OUT_DIR%\virtualloop.inf" /install
"%DEVCON%" install "%OUT_DIR%\virtualloop.inf" ROOT\VirtualLoopMic

echo === Step 6: Restart audio services ===
net start AudioEndpointBuilder >nul 2>&1
timeout /t 2 /nobreak >nul
net start Audiosrv >nul 2>&1

echo === Step 7: Check ===
timeout /t 3 /nobreak >nul
"%DEVCON%" status ROOT\VirtualLoopMic

echo.
echo Done. Check Sound Settings - volume should be 100%%.
pause
