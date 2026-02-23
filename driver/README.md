# Virtual Loop Mic — WDM Kernel-Mode Audio Driver

## Overview

Virtual Loop Mic is a Windows WDM (Windows Driver Model) kernel-mode audio
capture driver based on the Microsoft Sysvad sample. It exposes a virtual
microphone recording device ("Virtual Loop Mic") that reads PCM audio data from
a shared memory region created by the DirectPipe host application.

This replaces the need for third-party virtual audio cables (e.g., VB-Cable).
Any Windows application (OBS, Discord, Teams, Zoom, games, etc.) can select
"Virtual Loop Mic" as a standard microphone input to receive DirectPipe-processed
audio with minimal latency.

For overall project architecture, see [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md).

## Architecture

```
DirectPipe Host App (user-mode)
      |
      | Writes float32 PCM via SPSC ring buffer
      v
  Shared Memory  ("\\BaseNamedObjects\\DirectPipeAudio")
      |
      | ZwOpenSection / ZwMapViewOfSection (kernel-mode)
      v
  Virtual Loop Mic Driver (WDM WaveRT miniport)
      |
      | Exposes KS capture pin
      v
  Windows Audio Engine (audiodg.exe)
      |
      v
  Any application that opens the "Virtual Loop Mic" recording device
```

## Files

| Path | Description |
|------|-------------|
| `src/adapter.cpp` | WDM adapter: DriverEntry, AddDevice, PnP dispatch |
| `src/miniport.cpp` | WaveRT miniport for capture-only virtual device |
| `src/stream.cpp` | WaveRT stream: fills DMA buffer from shared memory |
| `src/shm_kernel_reader.cpp` | Kernel-mode shared memory reader (Zw* APIs) |
| `inf/virtualloop.inf` | INF install file for the driver |
| `test/driver_test.cpp` | User-mode test for shared memory read logic |

## Build Requirements

- **Windows 10/11 SDK** (10.0.19041.0 or later)
- **Windows Driver Kit (WDK)** matching the SDK version
- **Visual Studio 2019 or 2022** with the "Desktop development with C++"
  and "Windows driver development" workloads installed
- **Spectre-mitigated libraries** (install via VS Installer individual components)

### Build Steps

1. Open `virtualloop.sln` (or create a WDK KMDF project referencing the source
   files) in Visual Studio.
2. Select **Release | x64** configuration.
3. Build the solution. The output is `virtualloop.sys` and `virtualloop.inf`.

Alternatively, build from the command line:

```cmd
msbuild virtualloop.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Test Signing

Windows requires all kernel drivers to be signed. During development, use test
signing:

```cmd
:: 1. Enable test signing (requires admin, reboot needed once)
bcdedit /set testsigning on

:: 2. Create a test certificate (one-time)
makecert -r -pe -ss PrivateCertStore -n "CN=DirectPipeTestCert" DirectPipeTest.cer

:: 3. Sign the driver
signtool sign /s PrivateCertStore /n "DirectPipeTestCert" /t http://timestamp.digicert.com /fd sha256 virtualloop.sys

:: 4. Verify signature
signtool verify /pa virtualloop.sys
```

For production release, the driver must be submitted to the
[Windows Hardware Dev Center](https://partner.microsoft.com/dashboard/hardware)
for attestation signing or WHQL certification.

## Installation

```cmd
:: Install the driver (elevated command prompt)
pnputil /add-driver virtualloop.inf /install

:: Verify the device appeared
devcon status "ROOT\VirtualLoopMic"
```

After installation, "Virtual Loop Mic" should appear in:
- Sound Settings > Input devices
- Device Manager > Sound, video and game controllers

## Uninstallation

```cmd
:: Remove the driver
pnputil /delete-driver virtualloop.inf /uninstall /force

:: Or use Device Manager: right-click the device > Uninstall > check "Delete driver"
```

## Debugging

- Use **WinDbg** with a kernel debugger attached (VM or kdnet).
- Enable verbose tracing via WPP or `DbgPrint` (controlled by build macros).
- Check `!devnode` and `!ks.dump` in WinDbg for KS topology issues.
- Event Viewer > Windows Logs > System for driver load/unload events.

## Limitations

- Capture-only: this driver does not render (play back) audio.
- Depends on the DirectPipe host app creating the shared memory region before
  the driver stream is opened. If the host is not running, the driver produces
  silence.
- Currently supports 48 kHz and 44.1 kHz sample rates, mono and stereo,
  16-bit PCM and 32-bit float formats.
