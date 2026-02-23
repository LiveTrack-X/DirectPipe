@echo off
REM ============================================================================
REM  Virtual Loop Mic Driver - Test Signing Script
REM  Creates a test certificate and signs the driver for development
REM ============================================================================
REM
REM  PREREQUISITES:
REM    1. Run build.bat first to produce virtualloop.sys
REM    2. Enable test signing (run as Admin, reboot required):
REM         bcdedit /set testsigning on
REM
REM  After signing, install with:
REM    pnputil /add-driver out\virtualloop.inf /install
REM
REM  To uninstall:
REM    pnputil /delete-driver virtualloop.inf /uninstall /force
REM
setlocal

set "WDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "WDK_BIN=%WDK_ROOT%\bin\10.0.22621.0\x64"
set "OUT_DIR=%~dp0out"
set "CERT_NAME=DirectPipeTestCert"
set "CERT_STORE=PrivateCertStore"

echo.
echo ============================================================
echo  Test Signing Virtual Loop Mic Driver
echo ============================================================
echo.

REM ── Check driver exists ────────────────────────────────────────────────────
if not exist "%OUT_DIR%\virtualloop.sys" (
    echo ERROR: virtualloop.sys not found. Run build.bat first.
    exit /b 1
)

REM ── Step 1: Create test certificate (if not already created) ───────────────
echo [1/4] Creating test certificate...

"%WDK_BIN%\makecert.exe" -r -pe -ss %CERT_STORE% -n "CN=%CERT_NAME%" -eku 1.3.6.1.5.5.7.3.3 "%OUT_DIR%\%CERT_NAME%.cer" 2>nul
if errorlevel 1 (
    echo       Certificate may already exist, continuing...
)

REM ── Step 2: Create catalog file (.cat) from INF ────────────────────────────
echo [2/4] Creating catalog file...

REM Create a temporary CDF for Inf2Cat
"%WDK_BIN%\Inf2Cat.exe" /driver:"%OUT_DIR%" /os:10_x64 /verbose 2>nul
if errorlevel 1 (
    echo       Inf2Cat failed, using SignTool directly on .sys...
    echo       (Catalog signing skipped - direct .sys signing will be used)
)

REM ── Step 3: Sign the driver with the test certificate ──────────────────────
echo [3/4] Signing virtualloop.sys...

"%WDK_BIN%\signtool.exe" sign /s %CERT_STORE% /n "%CERT_NAME%" /t http://timestamp.digicert.com /fd SHA256 "%OUT_DIR%\virtualloop.sys"
if errorlevel 1 (
    echo       Timestamp server failed, signing without timestamp...
    "%WDK_BIN%\signtool.exe" sign /s %CERT_STORE% /n "%CERT_NAME%" /fd SHA256 "%OUT_DIR%\virtualloop.sys"
    if errorlevel 1 (
        echo ERROR: Failed to sign virtualloop.sys
        exit /b 1
    )
)

REM Sign the catalog too if it exists
if exist "%OUT_DIR%\virtualloop.cat" (
    echo [3b]  Signing virtualloop.cat...
    "%WDK_BIN%\signtool.exe" sign /s %CERT_STORE% /n "%CERT_NAME%" /t http://timestamp.digicert.com /fd SHA256 "%OUT_DIR%\virtualloop.cat"
)

REM ── Step 4: Verify signature ───────────────────────────────────────────────
echo [4/4] Verifying signature...

"%WDK_BIN%\signtool.exe" verify /pa /v "%OUT_DIR%\virtualloop.sys" >nul 2>&1
if errorlevel 1 (
    echo       Note: Verification may fail until test signing is enabled.
    echo       Run as Admin: bcdedit /set testsigning on
    echo       Then reboot.
) else (
    echo       Signature verified OK.
)

echo.
echo ============================================================
echo  SIGNING COMPLETE
echo.
echo  Files in %OUT_DIR%:
echo    virtualloop.sys  (signed)
echo    virtualloop.inf
echo    virtualloop.cat  (if Inf2Cat succeeded)
echo.
echo  Next steps:
echo    1. Enable test signing (Admin CMD):
echo         bcdedit /set testsigning on
echo       Then reboot.
echo.
echo    2. Install driver (Admin CMD):
echo         pnputil /add-driver "%OUT_DIR%\virtualloop.inf" /install
echo.
echo    3. Verify in Device Manager:
echo         Sound, video and game controllers ^> Virtual Loop Mic
echo.
echo    4. To uninstall (Admin CMD):
echo         pnputil /delete-driver virtualloop.inf /uninstall /force
echo ============================================================
echo.

exit /b 0
