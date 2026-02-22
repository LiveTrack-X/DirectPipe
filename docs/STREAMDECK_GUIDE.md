# DirectPipe Stream Deck Integration Guide

This guide covers how to install, configure, and use the DirectPipe Stream Deck plugin to control your audio processing from physical buttons.

## Overview

The DirectPipe Stream Deck plugin connects to the DirectPipe host application over WebSocket (`ws://localhost:8765`) and provides four button actions:

| Action           | Description                                        |
|------------------|----------------------------------------------------|
| **Bypass Toggle** | Toggle VST plugin bypass. Long-press for master bypass. |
| **Volume Control** | Adjust volume for input, virtual mic, or monitor. Press to toggle mute, rotate dial to adjust level. |
| **Preset Switch** | Load a preset by index, or cycle through presets.  |
| **Panic Mute**   | Immediately mute all audio outputs. Red indicator when muted. |

---

## Prerequisites

- **DirectPipe** v3.1 or later installed and running
- **Elgato Stream Deck** software v6.4 or later
- **Node.js** v18 or later (bundled with the plugin installer)

---

## Installation

### Method 1: From Release Package

1. Download `com.directpipe.streamdeck.streamDeckPlugin` from the DirectPipe releases page.
2. Double-click the file to install it into the Stream Deck software.
3. The plugin appears in the "DirectPipe Audio" category in the Stream Deck action list.

### Method 2: Manual Installation

1. Navigate to the Stream Deck plugins directory:
   ```
   Windows: %APPDATA%\Elgato\StreamDeck\Plugins\
   ```

2. Copy the `com.directpipe.sdPlugin` folder into the plugins directory:
   ```
   com.directpipe.sdPlugin/
     manifest.json
     package.json
     src/
       plugin.js
       websocket-client.js
       actions/
         bypass-toggle.js
         panic-mute.js
       inspectors/
         bypass-pi.html
         volume-pi.html
         preset-pi.html
     images/
       directpipe.png
       directpipe@2x.png
       ...
   ```

3. Install Node.js dependencies:
   ```bash
   cd com.directpipe.sdPlugin
   npm install
   ```

4. Restart the Stream Deck software.

---

## WebSocket Connection

The plugin connects to DirectPipe via WebSocket on `ws://localhost:8765`.

### Connection Lifecycle

1. Stream Deck software launches the plugin process.
2. Plugin registers with the Stream Deck SDK.
3. Plugin opens a WebSocket connection to `ws://localhost:8765`.
4. DirectPipe sends the current application state.
5. Plugin updates all button displays.

### Auto-Reconnect

If DirectPipe is not running or the connection is lost, the plugin will automatically reconnect with exponential backoff:

- Initial delay: 2 seconds
- Maximum delay: 30 seconds
- Backoff factor: 1.5x per attempt

While disconnected, all buttons show an alert indicator. Once reconnected, buttons automatically update to reflect the current state.

### Custom Port

If DirectPipe is configured to use a different WebSocket port, edit the `DIRECTPIPE_WS_URL` constant in `src/plugin.js`:

```javascript
const DIRECTPIPE_WS_URL = "ws://localhost:8770";
```

---

## Available Actions

### Bypass Toggle

**UUID:** `com.directpipe.bypass-toggle`

Toggles VST plugin bypass for a single plugin in the processing chain.

**Button behavior:**
- **Short press** — Toggle bypass for the configured plugin index
- **Long press** (hold > 500ms) — Toggle master bypass for the entire chain

**Button display:**
- Shows the plugin name and bypass/active state
- State 0 (bypassed): Shows plugin name with "Bypassed" label
- State 1 (active): Shows plugin name with "Active" label
- When master bypass is active: Shows "MASTER Bypassed"
- When plugin slot is empty: Shows "Slot N Empty"

**Settings (Property Inspector):**
| Setting       | Type   | Default | Description                     |
|---------------|--------|---------|---------------------------------|
| `pluginIndex` | number | 0       | Index of the plugin in the chain (0-based) |

**Example: Set up bypass buttons for a 3-plugin chain:**
1. Drag three "Bypass Toggle" actions to your Stream Deck.
2. Configure the first with `pluginIndex = 0`.
3. Configure the second with `pluginIndex = 1`.
4. Configure the third with `pluginIndex = 2`.

---

### Volume Control

**UUID:** `com.directpipe.volume-control`

Adjusts volume for a specific audio target. Supports both standard buttons and Stream Deck+ dials.

**Button behavior:**
- **Press** — Toggle mute for the target
- **Dial rotate** (Stream Deck+) — Adjust volume in 5% increments per tick
- **Dial long touch** — Reset volume to 100%

**Button display:**
- State 0: Volume is active, shows "Volume"
- State 1: Volume is muted, shows "Muted"

**Settings (Property Inspector):**
| Setting  | Type   | Default   | Description                        |
|----------|--------|-----------|------------------------------------|
| `target` | string | `monitor` | Volume target: `input`, `virtual_mic`, or `monitor` |

**Example: Create a monitor volume knob on Stream Deck+:**
1. Drag "Volume Control" to a dial position on your Stream Deck+.
2. Set `target` to `monitor`.
3. Rotate the dial to adjust volume. Press to toggle mute.

---

### Preset Switch

**UUID:** `com.directpipe.preset-switch`

Loads a specific preset or cycles through presets.

**Button behavior:**
- **Press with `presetIndex` configured** — Load the specific preset
- **Press without `presetIndex`** — Switch to the next preset

**Settings (Property Inspector):**
| Setting       | Type   | Default     | Description                        |
|---------------|--------|-------------|------------------------------------|
| `presetIndex` | number | (none)      | Specific preset index to load. If not set, cycles to next preset. |

**Example: Set up preset buttons:**
1. Drag a "Preset Switch" action and set `presetIndex = 0` for your "Streaming" preset.
2. Drag another and set `presetIndex = 1` for your "Recording" preset.
3. Drag a third without setting `presetIndex` to cycle through all presets.

---

### Panic Mute

**UUID:** `com.directpipe.panic-mute`

Emergency mute button that immediately silences all DirectPipe audio outputs.

**Button behavior:**
- **Press when unmuted** — Mute all audio outputs immediately
- **Press when muted** — Unmute all audio outputs

**Button display:**
- State 0 (live): Shows "MUTE" with normal appearance
- State 1 (muted): Shows "MUTED" with red indicator

No settings are required. This action works identically for every button instance.

**Recommended placement:** Place this in an easily reachable position on your Stream Deck for quick access during live streaming or recording.

---

## Multi-Action Support

All four actions support Stream Deck Multi-Actions, allowing you to combine DirectPipe controls with other actions in a sequence.

**Example Multi-Action: "Go Live"**
1. Load the "Streaming" preset (Preset Switch with `presetIndex = 0`)
2. Ensure bypass is off (Bypass Toggle)
3. Start OBS recording (OBS action)

---

## Troubleshooting

### Plugin shows alert indicators on all buttons

The plugin cannot connect to DirectPipe. Check:
- DirectPipe is running
- WebSocket server is enabled in DirectPipe settings (Settings > Control)
- The WebSocket port matches (default: 8765)
- No firewall is blocking localhost connections

### Button titles are not updating

State updates are pushed from DirectPipe over WebSocket. If titles are stale:
1. Check the DirectPipe log for WebSocket errors
2. Verify the connection count in DirectPipe status bar (should show "WS: 1")
3. Try removing and re-adding the button

### Plugin process keeps restarting

Check the Stream Deck plugin log:
```
Windows: %APPDATA%\Elgato\StreamDeck\logs\
```

Common causes:
- Missing Node.js dependencies (run `npm install` in the plugin directory)
- Node.js version too old (requires v18+)
- Syntax errors in plugin files

### WebSocket port conflict

If port 8765 is already in use by another application:
1. Open DirectPipe Settings > Control > Server Ports
2. Change the WebSocket port
3. Update `DIRECTPIPE_WS_URL` in `src/plugin.js` to match
4. Restart the Stream Deck software

---

## Protocol Reference

For the complete WebSocket and HTTP API documentation, including all action message formats and state object fields, see [CONTROL_API.md](./CONTROL_API.md).

---

## File Structure

```
streamdeck-plugin/
  package.json              # Node.js package definition
  manifest.json             # Stream Deck plugin manifest
  src/
    plugin.js               # Main plugin entry point
    websocket-client.js     # DirectPipe WebSocket client with auto-reconnect
    actions/
      bypass-toggle.js      # Bypass toggle action handler
      panic-mute.js         # Panic mute action handler
    inspectors/
      bypass-pi.html        # Property Inspector for bypass settings
      volume-pi.html        # Property Inspector for volume settings
      preset-pi.html        # Property Inspector for preset settings
  images/
    directpipe.png          # Plugin icon
    bypass-active.png       # Bypass active state icon
    bypass-off.png          # Bypass off state icon
    volume-on.png           # Volume active icon
    volume-muted.png        # Volume muted icon
    preset-active.png       # Preset icon
    panic-off.png           # Panic mute off icon
    panic-on.png            # Panic mute on icon (red)
    category.png            # Category icon
```
