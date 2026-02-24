# DirectPipe Stream Deck Plugin — Release Notes

## v1.1.0.0 (2026-02-24)

### Bug Fixes
- **Bypass Toggle**: Fixed 0-based plugin numbering — now correctly starts from Plugin #1 (was counting from 0)
- **Property Inspector**: Updated Bypass Toggle settings to use 1-based "Plugin #" field for clarity
- **SDKVersion**: Fixed manifest SDKVersion from 3 to 2 (correct for @elgato/streamdeck v2.x)
- **Async settings**: Fixed `getSettings()` calls to properly await (async) in bypass-toggle, volume-control, preset-switch actions
- **Volume Control**: Fixed input mute detection to check both `muted` and `input_muted` state fields
- **WebSocket Client**: Fixed pending message queue — now capped at 50 to prevent memory growth during prolonged disconnection

### Improvements
- **SDK v2 Migration**: Fully migrated to `@elgato/streamdeck` SDK v2 with `SingletonAction` class-based architecture
- **WebSocket Client**: Auto-reconnect with exponential backoff (2s -> 30s max) for reliable DirectPipe connection
- **State Sync**: Real-time state broadcast — all buttons update instantly when DirectPipe state changes
- **Connection Alert**: Visual alert on all action buttons when DirectPipe is disconnected
- **Ping keepalive**: 15-second ping interval to maintain connection

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
