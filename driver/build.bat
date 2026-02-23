@echo off
REM ============================================================================
REM  Virtual Loop Mic Driver - Build Script
REM  Compiles the WDM kernel driver using MSVC + WDK (no VS extension needed)
REM ============================================================================
setlocal enabledelayedexpansion

REM ── Paths ──────────────────────────────────────────────────────────────────
set "MSVC_ROOT=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.42.34433"
set "WDK_ROOT=C:\Program Files (x86)\Windows Kits\10"
set "WDK_VER=10.0.26100.0"

set "COMPILER=%MSVC_ROOT%\bin\Hostx64\x64\cl.exe"
set "LINKER=%MSVC_ROOT%\bin\Hostx64\x64\link.exe"

set "WDK_INC_KM=%WDK_ROOT%\Include\%WDK_VER%\km"
set "WDK_INC_KM_CRT=%WDK_ROOT%\Include\%WDK_VER%\km\crt"
set "WDK_INC_SHARED=%WDK_ROOT%\Include\%WDK_VER%\shared"
set "WDK_INC_SHARED_COMPAT=%WDK_ROOT%\Include\10.0.22621.0\shared"
set "WDK_LIB_KM=%WDK_ROOT%\Lib\%WDK_VER%\km\x64"
set "MSVC_LIB=%MSVC_ROOT%\lib\x64"

set "SRC_DIR=%~dp0src"
set "INF_DIR=%~dp0inf"
set "OUT_DIR=%~dp0out"

REM ── Unset CL/LINK env vars (they override compiler/linker options) ─────────
set "CL="
set "LINK="
set "_CL_="
set "_LINK_="

REM ── Create output directory ────────────────────────────────────────────────
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

echo.
echo ============================================================
echo  Building Virtual Loop Mic Driver (x64 Release)
echo ============================================================
echo.

REM ── Compile each source file ───────────────────────────────────────────────
for %%F in (adapter miniport stream shm_kernel_reader) do (
    echo Compiling %%F.cpp ...
    "%COMPILER%" /kernel /Zi /W4 /GS- /Gy /GR- /EHs-c- /Zc:wchar_t /O2 /D_AMD64_ /DAMD64 /D_WIN64 /DNTDDI_VERSION=0x0A000007 /DWINVER=0x0A00 /DWINNT=1 /DPOOL_NX_OPTIN=1 /I"%WDK_INC_KM%" /I"%WDK_INC_KM_CRT%" /I"%WDK_INC_SHARED%" /I"%WDK_INC_SHARED_COMPAT%" /c "%SRC_DIR%\%%F.cpp" /Fo"%OUT_DIR%\%%F.obj" /Fd"%OUT_DIR%\virtualloop.pdb"
    if errorlevel 1 (
        echo ERROR: Failed to compile %%F.cpp
        exit /b 1
    )
    echo.
)

echo ── Linking virtualloop.sys ──
echo.

"%LINKER%" /DRIVER:WDM /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /OUT:"%OUT_DIR%\virtualloop.sys" /MAP:"%OUT_DIR%\virtualloop.map" /PDB:"%OUT_DIR%\virtualloop.pdb" /DEBUG /NODEFAULTLIB /LIBPATH:"%WDK_LIB_KM%" /LIBPATH:"%MSVC_LIB%" ntoskrnl.lib hal.lib portcls.lib drmk.lib ks.lib ksguid.lib stdunk.lib BufferOverflowFastFailK.lib wdmsec.lib ntstrsafe.lib "%OUT_DIR%\adapter.obj" "%OUT_DIR%\miniport.obj" "%OUT_DIR%\stream.obj" "%OUT_DIR%\shm_kernel_reader.obj"

if errorlevel 1 (
    echo ERROR: Link failed!
    exit /b 1
)

echo.
echo ── Copying INF ──
copy /Y "%INF_DIR%\virtualloop.inf" "%OUT_DIR%\virtualloop.inf" >nul

echo.
echo ============================================================
echo  BUILD SUCCESSFUL
echo  Output: %OUT_DIR%\virtualloop.sys
echo ============================================================
echo.

exit /b 0
