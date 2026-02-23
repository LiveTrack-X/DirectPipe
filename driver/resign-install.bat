@echo off
REM Re-sign with exact thumbprint and reinstall
setlocal

net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: Run as Administrator
    pause
    exit /b 1
)

set "WDK_BIN=C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64"
set "DEVCON=C:\Program Files (x86)\Windows Kits\10\Tools\10.0.26100.0\x64\devcon.exe"
set "OUT_DIR=%~dp0out"

echo.
echo [1/5] Finding DirectPipeTestCert thumbprint...
for /f "tokens=*" %%T in ('powershell -NoProfile -Command "(Get-ChildItem Cert:\LocalMachine\My | Where-Object { $_.Subject -eq 'CN=DirectPipeTestCert' } | Sort-Object NotAfter -Descending | Select-Object -First 1).Thumbprint"') do set "THUMBPRINT=%%T"

if "%THUMBPRINT%"=="" (
    echo ERROR: Certificate not found in LocalMachine\My
    pause
    exit /b 1
)
echo       Thumbprint: %THUMBPRINT%

echo [2/5] Signing virtualloop.sys with SHA1 thumbprint...
"%WDK_BIN%\signtool.exe" sign /sha1 %THUMBPRINT% /sm /fd SHA256 "%OUT_DIR%\virtualloop.sys"
if errorlevel 1 (
    echo ERROR: Failed to sign .sys
    pause
    exit /b 1
)

echo [3/5] Signing virtualloop.cat with SHA1 thumbprint...
if exist "%OUT_DIR%\virtualloop.cat" (
    "%WDK_BIN%\signtool.exe" sign /sha1 %THUMBPRINT% /sm /fd SHA256 "%OUT_DIR%\virtualloop.cat"
    if errorlevel 1 (
        echo WARNING: Failed to sign .cat
    )
)

echo [4/5] Verifying signature...
"%WDK_BIN%\signtool.exe" verify /v /pa "%OUT_DIR%\virtualloop.sys" 2>&1 | findstr /i "Issued\|SHA1\|Successfully"

echo [5/5] Removing old device and reinstalling...
"%DEVCON%" remove ROOT\VirtualLoopMic >nul 2>&1
pnputil /delete-driver oem138.inf /uninstall /force >nul 2>&1
timeout /t 2 /nobreak >nul

pnputil /add-driver "%OUT_DIR%\virtualloop.inf" /install
if errorlevel 1 (
    echo WARNING: pnputil add-driver failed, trying devcon...
)

"%DEVCON%" install "%OUT_DIR%\virtualloop.inf" ROOT\VirtualLoopMic

echo.
echo Checking device status...
"%DEVCON%" status ROOT\VirtualLoopMic

echo.
pause
