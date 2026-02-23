# DirectPipe User Guide

## What is DirectPipe?

DirectPipe lets you apply VST2/VST3 audio effects to your USB microphone and send the processed audio to Discord, Zoom, OBS, and any other application — all with ultra-low latency (13-23ms), no hardware audio interface needed.

It replaces the "Light Host + VB-Cable" setup with a single application that includes a built-in virtual microphone driver ("Virtual Loop Mic").

## Quick Start

1. **Install DirectPipe** (includes the Virtual Loop Mic driver)
2. **Launch DirectPipe**
3. **Select your driver type** — ASIO or Windows Audio (WASAPI) in the Audio tab
4. **Select your microphone** from the device dropdown
5. **Scan for VST plugins** — click "Scan..." to find all installed plugins
6. **Add VST plugins** to the chain — click "+ Add Plugin" and select from the scanned list
7. **Configure monitor output** in the Output tab if you want to hear yourself

## Audio Settings (Audio Tab)

### Driver Type

- **Windows Audio (WASAPI)**: Default. Non-exclusive access — other apps can use your mic simultaneously. Separate input/output device selection.
- **ASIO**: Lower latency. Single device selection. Dynamic sample rate and buffer size lists from the device. "ASIO Control Panel" button to open the driver's native settings.

### Sample Rate & Buffer Size

**WASAPI mode**: Fixed list of common values (44100, 48000 Hz; 64-2048 samples).

**ASIO mode**: Lists only what the device actually supports. Change via the ASIO Control Panel for best results.

| Buffer (samples @48kHz) | Latency   | Notes |
|--------------------------|-----------|-------|
| 480 (default)            | ~23ms     | Stable for most systems |
| 256                      | ~13ms     | Good balance |
| 128                      | ~8ms      | Low latency, needs decent CPU |
| 64                       | ~5ms      | Ultra-low, may glitch on slower systems |

### Channel Mode

- **Stereo** (default): Channels pass through as-is.
- **Mono**: Both microphone channels are mixed to mono. Best for voice.

## VST Plugin Chain

DirectPipe supports both **VST2** (.dll) and **VST3** (.vst3) plugins in a serial processing chain.

- **Add**: Click "+ Add Plugin" and select from scanned plugins or browse for a file
- **Remove**: Click the **X** button on a plugin row
- **Reorder**: **Drag and drop** a plugin row to a new position
- **Bypass**: Click the **Bypass** toggle to skip a plugin without removing it
- **Edit**: Click **Edit** to open the plugin's native GUI window

### Plugin Internal State

Plugin parameters (EQ curves, compressor settings, etc.) are automatically saved and restored:
- When switching between preset slots A-E
- When closing a plugin editor window
- On application exit

## Quick Preset Slots (A-E)

Five quick-access slots for different VST chain configurations.

- **Click a slot** to switch to it (current slot is saved first)
- **Same plugins**: Switching is instant (only bypass and parameters change)
- **Different plugins**: Loads asynchronously — UI stays responsive
- **Active slot** is highlighted in purple
- **Occupied slots** show a lighter color
- **Empty slots** are dimmed

Slots save **chain-only data** (plugins, order, bypass, plugin parameters). Audio settings and output configuration are NOT affected by slot switching.

## Output Settings (Output Tab)

### Monitor Output

- **Device**: Select which output device to use for monitoring
- **Volume**: Adjust monitor volume
- **Enable**: Toggle monitor on/off (default: off)

Monitor output lets you hear your processed audio through headphones while streaming.

## VST Plugin Scanner

DirectPipe includes an **out-of-process VST scanner** that safely discovers all installed plugins.

1. Click **"Scan..."** in the plugin chain area
2. Default directories are pre-configured (VST3, VST2, Steinberg paths)
3. Click **"Scan for Plugins"** — scanning runs in a separate process
4. If a bad plugin crashes the scanner, it automatically retries (up to 5 times), skipping the problematic plugin

## System Tray

- **Close button**: Minimizes to system tray (app continues running)
- **Tray icon double-click**: Shows the main window
- **Tray icon right-click**: Menu with "Show Window" and "Quit DirectPipe"

## External Control

DirectPipe can be controlled while minimized or in the background.

### Keyboard Shortcuts

| Default Shortcut    | Action |
|---------------------|--------|
| Ctrl+Shift+1-3      | Toggle Plugin 1-3 Bypass |
| Ctrl+Shift+0        | Master Bypass |
| Ctrl+Shift+M        | Panic Mute (all outputs) |
| Ctrl+Shift+N        | Input Mute Toggle |
| Ctrl+Shift+Up/Down  | Input gain +/- 1dB |
| Ctrl+Shift+F1-F8    | Load preset 1-8 |

### Panic Mute

Panic Mute immediately silences all outputs. When unmuted, the previous enable states are restored (monitor on/off, virtual cable on/off remembered from before mute).

### MIDI Control

1. Open Controls tab > MIDI section
2. Select your MIDI device
3. Click [Learn] next to an action
4. Move a knob/press a button on your controller
5. The mapping is saved automatically

### Stream Deck

See [Stream Deck Guide](STREAMDECK_GUIDE.md) for detailed setup.

### HTTP API

```bash
curl http://127.0.0.1:8766/api/bypass/0/toggle
curl http://127.0.0.1:8766/api/mute/panic
curl http://127.0.0.1:8766/api/volume/monitor/0.5
```

See [Control API Reference](CONTROL_API.md) for all endpoints.

## Troubleshooting

**No audio input?**
- Check the correct microphone is selected in Audio tab
- Verify the microphone works in Windows Sound Settings
- Try switching between WASAPI and ASIO driver types

**Glitches or dropouts?**
- Increase the buffer size
- For ASIO, use the ASIO Control Panel to adjust buffer size
- Close other audio-intensive applications

**Virtual Loop Mic not appearing?**
- Ensure the driver is installed (check Device Manager)
- Reinstall DirectPipe with the driver option enabled

**Slot switching is slow?**
- First load of different plugins takes time (plugin initialization)
- Subsequent switches between the same plugins are instant
- UI remains responsive during async loading

**Plugin settings lost after slot switch?**
- Plugin internal state is saved when: switching slots, closing editor, on app exit
- If you change parameters and immediately switch, the current slot is saved first
