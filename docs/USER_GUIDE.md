# DirectPipe User Guide

## What is DirectPipe?

DirectPipe lets you apply VST2/VST3 audio effects to your USB microphone and send the processed audio to Discord, Zoom, OBS, and any other application — all with ultra-low latency (13-23ms), no hardware audio interface needed.

It replaces the "Light Host + VB-Cable" setup with a single application that includes a built-in virtual microphone driver ("Virtual Loop Mic").

## Quick Start

1. **Install DirectPipe** (includes the Virtual Loop Mic driver)
2. **Launch DirectPipe**
3. **Select your microphone** from the INPUT dropdown
4. **Add VST plugins** to the chain (click "+ Add VST Plugin")
5. **Configure outputs**:
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

- **Mono**: Both microphone channels are mixed to mono (0.5 L + 0.5 R average). Best for voice.
- **Stereo**: Channels pass through as-is. Use for stereo microphones.

### Buffer Size

| Buffer (samples @48kHz) | Latency   | Notes |
|--------------------------|-----------|-------|
| 480 (default)            | ~23ms     | Stable for most systems |
| 256                      | ~13ms     | Good balance |
| 128                      | ~8ms      | Low latency, needs decent CPU |
| 64                       | ~5ms      | Ultra-low, may glitch on slower systems |

### Sample Rate

48000 Hz is the default and recommended for most use cases. 44100 Hz is also supported.

## VST Plugin Chain

DirectPipe supports both **VST2** and **VST3** plugins in a serial processing chain.

- **Add**: Click "+ Add VST Plugin" and browse to a .dll (VST2) or .vst3 file
- **Remove**: Click the X button on a plugin slot
- **Reorder**: Drag plugins up/down in the chain
- **Bypass**: Click the Bypass button to skip a plugin without removing it
- **Edit**: Click Edit to open the plugin's native GUI

### Recommended Free VST Plugins

- **ReaPlugs** (ReaEQ, ReaComp, ReaGate) — by Cockos
- **TDR Nova** — by Tokyo Dawn Records
- **MeldaProduction MFreeFXBundle** — comprehensive free bundle

## External Control

DirectPipe can be controlled while minimized or in the background.

### Keyboard Shortcuts

Global hotkeys work even when DirectPipe is not focused.

| Default Shortcut  | Action |
|-------------------|--------|
| Ctrl+Shift+1      | Toggle Plugin 1 bypass |
| Ctrl+Shift+2      | Toggle Plugin 2 bypass |
| Ctrl+Shift+0      | Toggle master bypass |
| Ctrl+Shift+M      | Panic mute (all outputs) |
| Ctrl+Shift+Up/Down | Input gain +/- 1dB |
| Ctrl+Shift+F1-F8  | Load preset 1-8 |

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
- Check the DirectPipe log for error messages

**High latency?**
- Reduce buffer size in Audio Settings
- Use fewer VST plugins in the chain
- Some plugins add internal latency — check plugin documentation
