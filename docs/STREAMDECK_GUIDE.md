# DirectPipe Stream Deck Integration Guide

## Overview

The DirectPipe Stream Deck plugin connects to the DirectPipe host application over WebSocket (`ws://localhost:8765`) and provides four button actions:

| Action           | Description                                        | Status |
|------------------|----------------------------------------------------|--------|
| **Bypass Toggle** | Toggle VST plugin bypass. Long-press for master bypass. | Complete |
| **Panic Mute**   | Immediately mute all audio outputs. Red indicator when muted. | Complete |
| **Volume Control** | Adjust volume for input, virtual mic, or monitor. Press to toggle mute, rotate dial to adjust level. | In progress |
| **Preset Switch** | Load a preset slot (A-E), or cycle through presets. | In progress |

---

## Prerequisites

- **DirectPipe** v3.1 or later installed and running
- **Elgato Stream Deck** software v6.4 or later
- **Node.js** v18 or later

---

## Installation

### Manual Installation

1. Navigate to the Stream Deck plugins directory:
   ```
   %APPDATA%\Elgato\StreamDeck\Plugins\
   ```

2. Copy the `com.directpipe.sdPlugin` folder into the plugins directory.

3. Install Node.js dependencies:
   ```bash
   cd com.directpipe.sdPlugin
   npm install
   ```

4. Restart the Stream Deck software.

---

## WebSocket Connection

The plugin connects to DirectPipe via WebSocket on `ws://localhost:8765`.

### Auto-Reconnect

If DirectPipe is not running or the connection is lost, the plugin will automatically reconnect with exponential backoff:

- Initial delay: 2 seconds
- Maximum delay: 30 seconds
- Backoff factor: 1.5x per attempt

While disconnected, all buttons show an alert indicator.

### Custom Port

Edit the `DIRECTPIPE_WS_URL` constant in `src/plugin.js`:

```javascript
const DIRECTPIPE_WS_URL = "ws://localhost:8770";
```

---

## Available Actions

### Bypass Toggle

**UUID:** `com.directpipe.bypass-toggle`

- **Short press** — Toggle bypass for the configured plugin index
- **Long press** (hold > 500ms) — Toggle master bypass

**Button display:**
- Shows plugin name + "Bypassed" or "Active"
- Master bypass shows "MASTER Bypassed"
- Empty slot shows "Slot N Empty"

**Settings:** `pluginIndex` (number, default: 0)

---

### Panic Mute

**UUID:** `com.directpipe.panic-mute`

- **Press** — Toggle panic mute on all outputs

**Button display:**
- State 0 (live): "MUTE" with normal appearance
- State 1 (muted): "MUTED" with red indicator

No settings required.

---

### Volume Control (In progress)

**UUID:** `com.directpipe.volume-control`

- **Press** — Toggle mute for the target
- **Dial rotate** (Stream Deck+) — Adjust volume ±5% per tick

**Settings:** `target` (string: `input`, `virtual_mic`, or `monitor`)

> Property Inspector HTML and button state display are not yet implemented.

---

### Preset Switch (In progress)

**UUID:** `com.directpipe.preset-switch`

- **Press with slot configured** — Switch to specific preset slot
- **Press without config** — Cycle to next preset

**Settings:** `slotIndex` (number: 0-4 for slots A-E) or `presetIndex` (number)

> Property Inspector HTML and button state display are not yet implemented.

---

## Troubleshooting

### Plugin shows alert indicators on all buttons

DirectPipe is not running, or the WebSocket connection failed. Check:
- DirectPipe is running
- WebSocket port matches (default: 8765)

### Plugin process keeps restarting

Check `%APPDATA%\Elgato\StreamDeck\logs\`. Common causes:
- Missing Node.js dependencies (run `npm install`)
- Node.js version too old (requires v18+)

---

## File Structure

```
streamdeck-plugin/
  manifest.json             # Stream Deck plugin manifest (SDK v2)
  package.json              # Node.js package (ws v8.16.0)
  src/
    plugin.js               # Main entry point, routes SD events to handlers
    websocket-client.js     # WebSocket client with auto-reconnect
    actions/
      bypass-toggle.js      # Bypass toggle (class-based, complete)
      panic-mute.js         # Panic mute (class-based, complete)
```

### Missing (to be created)

```
  src/
    actions/
      volume-control.js     # Volume control action class
      preset-switch.js      # Preset switch action class
    inspectors/
      bypass-pi.html        # Property Inspector for bypass settings
      volume-pi.html        # Property Inspector for volume settings
      preset-pi.html        # Property Inspector for preset settings
  images/                   # Button icons (72x72 PNG, @2x 144x144)
```

---

## Protocol Reference

See [CONTROL_API.md](./CONTROL_API.md) for the complete WebSocket and HTTP API documentation.
