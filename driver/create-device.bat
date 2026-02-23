@echo off
net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: Run as Administrator
    pause
    exit /b 1
)
"C:\Program Files (x86)\Windows Kits\10\Tools\10.0.26100.0\x64\devcon.exe" install "%~dp0out\virtualloop.inf" ROOT\VirtualLoopMic
echo.
echo Done. Check Device Manager for "Virtual Loop Mic"
pause
