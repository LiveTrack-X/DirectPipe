@echo off
REM Full rebuild + sign + reinstall + set volume 100%%
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: Run as Administrator
    pause
    exit /b 1
)

set "WDK_BIN=C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64"
set "INF2CAT=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\Inf2Cat.exe"
set "DEVCON=C:\Program Files (x86)\Windows Kits\10\Tools\10.0.26100.0\x64\devcon.exe"
set "OUT_DIR=%~dp0out"

echo.
echo === Step 1: Build ===
call "%~dp0build.bat"
if errorlevel 1 (
    echo BUILD FAILED
    pause
    exit /b 1
)

echo === Step 2: Create catalog ===
del /f "%OUT_DIR%\virtualloop.cat" >nul 2>&1
"%INF2CAT%" /driver:"%OUT_DIR%" /os:10_x64 /verbose
if errorlevel 1 (
    echo WARNING: Inf2Cat failed
)

echo === Step 3: Sign with test cert ===
for /f "tokens=*" %%T in ('powershell -NoProfile -Command "(Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq 'CN=DirectPipeTestCert' } | Sort-Object NotAfter -Descending | Select-Object -First 1).Thumbprint"') do set "THUMBPRINT=%%T"
echo Thumbprint: %THUMBPRINT%

"%WDK_BIN%\signtool.exe" sign /sha1 %THUMBPRINT% /sm /fd SHA256 "%OUT_DIR%\virtualloop.sys"
if errorlevel 1 (
    echo ERROR: Failed to sign .sys
    pause
    exit /b 1
)
if exist "%OUT_DIR%\virtualloop.cat" (
    "%WDK_BIN%\signtool.exe" sign /sha1 %THUMBPRINT% /sm /fd SHA256 "%OUT_DIR%\virtualloop.cat"
)

echo === Step 4: Stop audio + remove old device ===
net stop Audiosrv >nul 2>&1
net stop AudioEndpointBuilder >nul 2>&1
"%DEVCON%" remove ROOT\VirtualLoopMic >nul 2>&1

REM Remove all OEM drivers for virtualloop
pnputil /enum-drivers /class "MEDIA" > "%TEMP%\pnp_enum.txt" 2>&1
powershell -NoProfile -ExecutionPolicy Bypass -Command "Get-Content '%TEMP%\pnp_enum.txt' | Select-String 'virtualloop' -Context 5,0 | ForEach-Object { if ($_.Context.PreContext -match '(oem\d+\.inf)') { Write-Host ('Removing: ' + $Matches[1]); pnputil /delete-driver $Matches[1] /uninstall /force 2>$null | Out-Null } }"
timeout /t 2 /nobreak >nul

echo === Step 5: Install ===
pnputil /add-driver "%OUT_DIR%\virtualloop.inf" /install
"%DEVCON%" install "%OUT_DIR%\virtualloop.inf" ROOT\VirtualLoopMic

echo === Step 6: Restart audio services ===
net start AudioEndpointBuilder >nul 2>&1
timeout /t 2 /nobreak >nul
net start Audiosrv >nul 2>&1
timeout /t 3 /nobreak >nul

echo === Step 7: Set volume to 100%% (SYSTEM) ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0set-volume-100.ps1"

echo === Step 8: Restart audio to apply volume ===
net stop Audiosrv >nul 2>&1
net stop AudioEndpointBuilder >nul 2>&1
net start AudioEndpointBuilder >nul 2>&1
timeout /t 2 /nobreak >nul
net start Audiosrv >nul 2>&1

echo === Step 9: Check status ===
timeout /t 3 /nobreak >nul
"%DEVCON%" status ROOT\VirtualLoopMic

echo.
echo Done. Check Sound Settings - "Virtual Loop Mic" volume should be 100%%.
pause
