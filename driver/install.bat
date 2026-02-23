@echo off
REM ============================================================================
REM  Virtual Loop Mic Driver - Complete Sign + Install Script
REM  Must run as Administrator
REM ============================================================================
setlocal enabledelayedexpansion

set "WDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "WDK_BIN=%WDK_ROOT%\bin\10.0.22621.0\x64"
set "INF2CAT=%WDK_ROOT%\bin\10.0.26100.0\x86\Inf2Cat.exe"
set "MAKECAT=%WDK_ROOT%\bin\10.0.26100.0\x86\makecat.exe"
set "OUT_DIR=%~dp0out"
set "CERT_NAME=DirectPipeTestCert"
set "CERT_STORE=PrivateCertStore"
set "CERT_SHA1=F2ECFEF5DBF7C94EF504632D5788995B89CAE571"

echo.
echo ============================================================
echo  Virtual Loop Mic Driver - Sign + Install
echo ============================================================
echo.

REM ── Check admin ─────────────────────────────────────────────────────────────
net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: This script must be run as Administrator.
    echo Right-click CMD ^> Run as administrator
    pause
    exit /b 1
)

REM ── Check driver exists ─────────────────────────────────────────────────────
if not exist "%OUT_DIR%\virtualloop.sys" (
    echo ERROR: virtualloop.sys not found. Run build.bat first.
    pause
    exit /b 1
)

REM ── Check test signing is enabled ───────────────────────────────────────────
echo [0/6] Checking test signing mode...
bcdedit /enum {current} | findstr /i "testsigning.*Yes" >nul 2>&1
if errorlevel 1 (
    echo       Test signing is NOT enabled. Enabling now...
    bcdedit /set testsigning on
    if errorlevel 1 (
        echo ERROR: Failed to enable test signing.
        pause
        exit /b 1
    )
    echo       Test signing enabled. REBOOT REQUIRED before install.
    echo       Run this script again after rebooting.
    pause
    exit /b 0
)
echo       Test signing is ON.

REM ── Step 1: Clean old certs, create fresh one ──────────────────────────────
echo [1/6] Creating test certificate...

REM Delete old certs from the private store to avoid duplicates
certutil -delstore %CERT_STORE% "%CERT_NAME%" >nul 2>&1
certutil -delstore %CERT_STORE% "%CERT_NAME%" >nul 2>&1

REM Delete old .cer file
del /f "%OUT_DIR%\%CERT_NAME%.cer" >nul 2>&1

REM Create a fresh self-signed certificate using PowerShell (more reliable)
powershell -NoProfile -ExecutionPolicy Bypass -Command "$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=DirectPipeTestCert' -CertStoreLocation 'Cert:\LocalMachine\My' -NotAfter (Get-Date).AddYears(10); Export-Certificate -Cert $cert -FilePath '%OUT_DIR%\DirectPipeTestCert.cer' -Force | Out-Null; Write-Host $cert.Thumbprint"
if errorlevel 1 (
    echo       PowerShell cert creation failed, trying makecert...
    "%WDK_BIN%\makecert.exe" -r -pe -ss My -sr LocalMachine -n "CN=%CERT_NAME%" -eku 1.3.6.1.5.5.7.3.3 "%OUT_DIR%\%CERT_NAME%.cer" 2>nul
)

if not exist "%OUT_DIR%\%CERT_NAME%.cer" (
    echo ERROR: Failed to create test certificate.
    pause
    exit /b 1
)
echo       Certificate created.

REM ── Step 2: Install cert into Trusted Root + Trusted Publishers ─────────────
echo [2/6] Installing test certificate into trusted stores...

certutil -addstore "Root" "%OUT_DIR%\%CERT_NAME%.cer" >nul 2>&1
if errorlevel 1 (
    echo       Warning: Could not add to Root store (may already exist)
) else (
    echo       Added to Trusted Root Certification Authorities
)

certutil -addstore "TrustedPublisher" "%OUT_DIR%\%CERT_NAME%.cer" >nul 2>&1
if errorlevel 1 (
    echo       Warning: Could not add to TrustedPublisher store (may already exist)
) else (
    echo       Added to Trusted Publishers
)

REM ── Step 3: Create catalog file (.cat) ──────────────────────────────────────
echo [3/6] Creating catalog file...

REM Delete old catalog
del /f "%OUT_DIR%\virtualloop.cat" >nul 2>&1

REM Try Inf2Cat first
if exist "%INF2CAT%" (
    echo       Using Inf2Cat...
    "%INF2CAT%" /driver:"%OUT_DIR%" /os:10_x64 /verbose
    if not errorlevel 1 (
        echo       Catalog created with Inf2Cat.
        goto :cat_done
    )
    echo       Inf2Cat failed, trying makecat...
)

REM Fallback: use makecat with CDF
echo       Creating CDF for makecat...
(
    echo [CatalogHeader]
    echo Name=virtualloop.cat
    echo PublicVersion=0x0000001
    echo EncodingType=0x00010001
    echo CATATTR1=0x10010001:OSAttr:2:10.0
    echo.
    echo [CatalogFiles]
    echo ^<hash^>virtualloop.inf=virtualloop.inf
    echo ^<hash^>virtualloop.sys=virtualloop.sys
) > "%OUT_DIR%\virtualloop.cdf"

if exist "%MAKECAT%" (
    pushd "%OUT_DIR%"
    "%MAKECAT%" "%OUT_DIR%\virtualloop.cdf"
    popd
) else (
    pushd "%OUT_DIR%"
    makecat "%OUT_DIR%\virtualloop.cdf" 2>nul
    popd
)

:cat_done
if not exist "%OUT_DIR%\virtualloop.cat" (
    echo       WARNING: No catalog file created. Will try install with .sys signature only.
) else (
    echo       Catalog file ready.
)

REM ── Step 4: Sign files ──────────────────────────────────────────────────────
echo [4/6] Signing driver files...

REM Use /a to auto-select best matching certificate
echo       Signing virtualloop.sys...
"%WDK_BIN%\signtool.exe" sign /a /s My /sr LocalMachine /n "%CERT_NAME%" /fd SHA256 "%OUT_DIR%\virtualloop.sys"
if errorlevel 1 (
    REM Try with /a only (auto-select)
    "%WDK_BIN%\signtool.exe" sign /a /fd SHA256 "%OUT_DIR%\virtualloop.sys"
    if errorlevel 1 (
        echo ERROR: Failed to sign virtualloop.sys
        pause
        exit /b 1
    )
)
echo       virtualloop.sys signed.

if exist "%OUT_DIR%\virtualloop.cat" (
    echo       Signing virtualloop.cat...
    "%WDK_BIN%\signtool.exe" sign /a /s My /sr LocalMachine /n "%CERT_NAME%" /fd SHA256 "%OUT_DIR%\virtualloop.cat"
    if errorlevel 1 (
        "%WDK_BIN%\signtool.exe" sign /a /fd SHA256 "%OUT_DIR%\virtualloop.cat"
    )
    echo       virtualloop.cat signed.
)

REM ── Step 5: Verify ──────────────────────────────────────────────────────────
echo [5/6] Verifying signatures...

"%WDK_BIN%\signtool.exe" verify /pa "%OUT_DIR%\virtualloop.sys" >nul 2>&1
if errorlevel 1 (
    echo       Note: /pa verify failed (normal for test certs)
) else (
    echo       virtualloop.sys OK
)

if exist "%OUT_DIR%\virtualloop.cat" (
    "%WDK_BIN%\signtool.exe" verify /pa "%OUT_DIR%\virtualloop.cat" >nul 2>&1
    if errorlevel 1 (
        echo       Note: catalog /pa verify failed (normal for test certs)
    ) else (
        echo       virtualloop.cat OK
    )
)

REM ── Step 6: Install driver ──────────────────────────────────────────────────
echo [6/6] Installing driver...

REM First uninstall if already installed
pnputil /enum-devices /class MEDIA 2>nul | findstr /i "VirtualLoop" >nul 2>&1
if not errorlevel 1 (
    echo       Removing previous installation...
    pnputil /remove-device "ROOT\VirtualLoopMic\0000" >nul 2>&1
    pnputil /delete-driver virtualloop.inf /uninstall /force >nul 2>&1
    timeout /t 2 /nobreak >nul
)

pnputil /add-driver "%OUT_DIR%\virtualloop.inf" /install
if errorlevel 1 (
    echo.
    echo ============================================================
    echo  pnputil install failed.
    echo.
    echo  Trying devcon fallback...
    echo ============================================================

    REM Try devcon
    where devcon >nul 2>&1
    if not errorlevel 1 (
        devcon install "%OUT_DIR%\virtualloop.inf" "ROOT\VirtualLoopMic"
    ) else (
        echo.
        echo  devcon not found. Try manual install:
        echo    1. Device Manager ^> Action ^> Add legacy hardware
        echo    2. "Install hardware that I manually select"
        echo    3. "Sound, video and game controllers"
        echo    4. "Have Disk" ^> Browse to: %OUT_DIR%
        echo    5. Select virtualloop.inf
        echo.
        pause
        exit /b 1
    )
)

echo.
echo ============================================================
echo  DONE! Check:
echo    Sound Settings ^> Input ^> "Virtual Loop Mic"
echo    Device Manager ^> Sound controllers ^> "Virtual Loop Mic"
echo.
echo  To uninstall:
echo    pnputil /delete-driver virtualloop.inf /uninstall /force
echo ============================================================
echo.

pause
exit /b 0
