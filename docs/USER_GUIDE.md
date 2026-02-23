# DirectPipe User Guide

## What is DirectPipe?

DirectPipe lets you apply VST2/VST3 audio effects to your USB microphone and send the processed audio to Discord, Zoom, OBS, and any other application — all with ultra-low latency (13-23ms), no hardware audio interface needed.

It replaces the "Light Host + VB-Cable" setup with a single application that includes a built-in virtual microphone driver ("Virtual Loop Mic").

## Quick Start

1. **Install DirectPipe** (includes the Virtual Loop Mic driver)
2. **Launch DirectPipe**
3. **Select your microphone** from the INPUT dropdown
4. **Scan for VST plugins** — click "Scan..." to find all installed plugins
5. **Add VST plugins** to the chain — click "+ Add Plugin" and select from the scanned list
6. **Configure outputs**:
   - Virtual Loop Mic: automatically available as a microphone in all apps
   - Monitor: enable to hear yourself through headphones

## Setting Up for Discord/Zoom

1. Launch DirectPipe — the Virtual Loop Mic driver is already installed
2. In Discord/Zoom/Teams, go to Audio Settings
3. Set your **Input Device** to **"Virtual Loop Mic"**
4. Speak into your USB microphone — the processed audio flows through automatically

No VB-Cable or third-party virtual audio cable needed.

## Setting Up for OBS Studio

### Method 1: Virtual Loop Mic (recommended)

1. In OBS, add a new **Audio Input Capture** source
2. Select **"Virtual Loop Mic"** as the device
3. Done — lowest latency path via shared memory

### Method 2: OBS Plugin (alternative)

1. Install DirectPipe with OBS plugin option
2. In OBS, add a new **Audio Source**
3. Select **"DirectPipe Audio"** from the source list
4. The plugin auto-connects when DirectPipe is running

## Audio Settings

### Channel Mode

- **Stereo** (default): Channels pass through as-is. Use for stereo microphones.
- **Mono**: Both microphone channels are mixed to mono (0.5 L + 0.5 R average). Best for voice.

### Buffer Size

| Buffer (samples @48kHz) | Latency   | Notes |
|--------------------------|-----------|-------|
| 480 (default)            | ~23ms     | Stable for most systems |
| 256                      | ~13ms     | Good balance |
| 128                      | ~8ms      | Low latency, needs decent CPU |
| 64                       | ~5ms      | Ultra-low, may glitch on slower systems |

### Sample Rate

48000 Hz is the default and recommended for most use cases. 44100 Hz is also supported.

## VST Plugin Scanner

DirectPipe includes an **out-of-process VST scanner** that safely discovers all installed plugins.

### Scanning for Plugins

1. Click **"Scan..."** in the plugin chain area
2. The scanner dialog shows default scan directories:
   - `C:\Program Files\Common Files\VST3`
   - `C:\Program Files (x86)\Common Files\VST3`
   - `C:\Program Files\Common Files\VST`
   - `C:\Program Files\VstPlugins`
   - `C:\Program Files (x86)\VstPlugins`
   - `C:\Program Files\Steinberg\VST3`
   - `C:\Program Files\Steinberg\VSTPlugins`
   - `C:\Program Files (x86)\Steinberg\VSTPlugins`
3. Add custom directories with "Add Directory" if needed
4. Click **"Scan for Plugins"** — scanning runs in a separate process
5. If a bad plugin crashes the scanner, it automatically retries (up to 5 times), skipping the problematic plugin
6. Scanned plugins are cached — no need to rescan next time

### Adding Plugins from Scan Results

1. Click **"+ Add Plugin"**
2. Select from **"Scanned Plugins"** submenu (grouped by vendor)
3. Or double-click a plugin in the scanner dialog

## VST Plugin Chain

DirectPipe supports both **VST2** (.dll) and **VST3** (.vst3) plugins in a serial processing chain.

- **Add**: Click "+ Add Plugin" and select from scanned plugins or browse for a file
- **Remove**: Click the **X** button on a plugin row
- **Reorder**: **Drag and drop** a plugin row to a new position in the chain
- **Bypass**: Click the **Bypass** toggle to skip a plugin without removing it
- **Edit**: Click **Edit** to open the plugin's native GUI window (click close to hide, re-click Edit to show again)

### Recommended Free VST Plugins

- **ReaPlugs** (ReaEQ, ReaComp, ReaGate) — by Cockos
- **TDR Nova** — by Tokyo Dawn Records
- **MeldaProduction MFreeFXBundle** — comprehensive free bundle

## System Tray

DirectPipe minimizes to the system tray when you click the close (X) button:

- **Close button**: Minimizes to system tray (app continues running in background)
- **Tray icon double-click**: Shows the main window
- **Tray icon right-click**: Menu with "Show Window" and "Quit DirectPipe"
- **Quit**: Use the tray menu "Quit DirectPipe" to fully exit the application

## External Control

DirectPipe can be controlled while minimized or in the background.

### Keyboard Shortcuts

Global hotkeys work even when DirectPipe is not focused.

| Default Shortcut    | Action |
|---------------------|--------|
| Ctrl+Shift+1        | Toggle Plugin 1 Bypass |
| Ctrl+Shift+2        | Toggle Plugin 2 Bypass |
| Ctrl+Shift+3        | Toggle Plugin 3 Bypass |
| Ctrl+Shift+0        | Master Bypass |
| Ctrl+Shift+M        | Panic Mute (all outputs) |
| Ctrl+Shift+N        | Input Mute Toggle |
| Ctrl+Shift+Up/Down  | Input gain +/- 1dB |
| Ctrl+Shift+F1-F8    | Load preset 1-8 |

Customize in Settings > Control > Hotkeys.

### MIDI Control

Connect any MIDI controller (nanoKONTROL, MIDI keyboard, etc.):

1. Open Settings > Control > MIDI
2. Select your MIDI device
3. Click [Learn] next to an action
4. Move a knob/press a button on your controller
5. The mapping is saved automatically

Supports CC Toggle, CC Continuous (for volume knobs), and Note On/Off.

### Stream Deck

See [Stream Deck Guide](STREAMDECK_GUIDE.md) for detailed setup.

### HTTP API

Control DirectPipe from any script or tool via HTTP:

```bash
# Toggle first plugin bypass
curl http://127.0.0.1:8766/api/bypass/0/toggle

# Panic mute
curl http://127.0.0.1:8766/api/mute/panic

# Set monitor volume to 50%
curl http://127.0.0.1:8766/api/volume/monitor/0.5
```

See [Control API Reference](CONTROL_API.md) for all endpoints.

## Troubleshooting

**No audio input?**
- Check that the correct microphone is selected in the INPUT dropdown
- Verify the microphone works in Windows Sound Settings
- Another app may be using the microphone in exclusive mode — close it

**Glitches or dropouts?**
- Increase the buffer size (480 -> 512 or higher)
- Close other audio-intensive applications
- Check CPU usage in the DirectPipe status bar

**Virtual Loop Mic not appearing?**
- Ensure the driver is installed (check Device Manager > Sound, video and game controllers)
- Reinstall DirectPipe with the driver option enabled
- On some systems, a reboot is required after driver installation

**Discord/Zoom not detecting Virtual Loop Mic?**
- Restart Discord/Zoom after installing DirectPipe
- Check that "Virtual Loop Mic" appears in Windows Sound Settings > Input

**VST plugin not loading?**
- Ensure the plugin is the correct architecture (64-bit)
- VST2 plugins must be .dll files, VST3 must be .vst3 files
- Some plugins require specific runtimes (Visual C++ Redistributable)
- Try scanning again — the scanner caches results and skips previously problematic plugins

**Plugin scanner crashes?**
- The scanner runs in a separate process — crashes don't affect DirectPipe
- Bad plugins are automatically skipped on retry
- Delete the cache file at `%AppData%\DirectPipe\plugin-cache.xml` to reset and rescan
- Delete the dead man's pedal file at `%AppData%\DirectPipe\scan-deadmanspedal.txt` to allow retrying previously skipped plugins

**High latency?**
- Reduce buffer size in Audio Settings
- Use fewer VST plugins in the chain
- Some plugins add internal latency — check plugin documentation
