# DirectPipe Stream Deck Plugin — Release Notes

## v1.4.0.0 (2026-02-26)

### New Features
- **Recording Toggle**: New action to start/stop audio recording from Stream Deck. Shows elapsed time (mm:ss) on the button while recording.

### Actions
| Action | Description |
|--------|-------------|
| **Bypass Toggle** | Toggle individual VST plugin bypass. Long-press (>500ms) for master bypass. |
| **Volume Control** | Mute toggle / volume up / volume down modes. SD+ dial support (+-5% per tick). |
| **Preset Switch** | Switch between Quick Preset Slots (A-E) or cycle through presets. |
| **Monitor Toggle** | Toggle monitor output on/off. Visual state indicator (green=ON, gray=OFF). |
| **Panic Mute** | Instantly mute all DirectPipe audio outputs. Press again to unmute. |
| **Recording Toggle** | Start/stop recording processed audio to WAV. Shows REC + mm:ss elapsed time. |

---

## v1.2.0.0 (2026-02-25)

### New Features
- **Monitor Toggle**: New action to toggle monitor output (headphones) on/off from Stream Deck
- **State Broadcasting**: Real-time state sync — monitor_enabled, volumes, mute status pushed to Stream Deck via WebSocket

### Actions
| Action | Description |
|--------|-------------|
| **Bypass Toggle** | Toggle individual VST plugin bypass. Long-press (>500ms) for master bypass. |
| **Volume Control** | Mute toggle / volume up / volume down modes. SD+ dial support (+-5% per tick). |
| **Preset Switch** | Switch between Quick Preset Slots (A-E) or cycle through presets. |
| **Monitor Toggle** | Toggle monitor output on/off. Visual state indicator (green=ON, gray=OFF). |
| **Panic Mute** | Instantly mute all DirectPipe audio outputs. Press again to unmute. |

---

## v1.1.0.0 (2026-02-25)

### Bug Fixes
- **Bypass Toggle**: Fixed 0-based plugin numbering — now correctly starts from Plugin #1
- **Property Inspector**: Updated Bypass Toggle settings to use 1-based "Plugin #" field
- **SDKVersion**: Fixed manifest SDKVersion from 3 to 2 (correct for @elgato/streamdeck v2.x)
- **Async settings**: Fixed `getSettings()` calls to properly await (async) in all actions
- **Volume Control**: Fixed input mute detection to check both `muted` and `input_muted` state fields
- **WebSocket Client**: Fixed pending message queue — capped at 50 to prevent memory growth

### Improvements
- **SDK v2 Migration**: Fully migrated to `@elgato/streamdeck` SDK v2 with `SingletonAction` class-based architecture
- **WebSocket Client**: Auto-reconnect with exponential backoff (2s → 30s max)
- **State Sync**: Real-time state broadcast — all buttons update instantly when DirectPipe state changes
- **Connection Alert**: Visual alert on all action buttons when DirectPipe is disconnected
- **Ping keepalive**: 15-second ping interval to maintain connection
- **Category Icon**: Updated to white monochrome per Elgato Marketplace guidelines

### Actions
| Action | Description |
|--------|-------------|
| **Bypass Toggle** | Toggle individual VST plugin bypass. Long-press (>500ms) for master bypass. |
| **Volume Control** | Mute toggle / volume up / volume down modes. SD+ dial support (+-5% per tick). |
| **Preset Switch** | Switch between Quick Preset Slots (A-E) or cycle through presets. |
| **Panic Mute** | Instantly mute all DirectPipe audio outputs. Press again to unmute. |

### Requirements
- Stream Deck software 6.9+
- DirectPipe host running with WebSocket server enabled (port 8765)
- Windows 10/11

---

## v1.0.0.0 (2026-02-24)

### Initial Release
- 4 actions: Bypass Toggle, Volume Control, Preset Switch, Panic Mute
- WebSocket connection to DirectPipe host (ws://localhost:8765)
- Property Inspector settings for each action (sdpi-components v4)
- Auto-generated SVG -> PNG icons (13 icon variants, @2x Retina support)
- Node.js 20 runtime
