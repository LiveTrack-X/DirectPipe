# DirectPipe Stream Deck Plugin — Release Notes

## v1.1.0.0 (2026-02-24)

### Bug Fixes
- **Bypass Toggle**: Fixed 0-based plugin numbering — now correctly starts from Plugin #1 (was counting from 0)
- **Property Inspector**: Updated Bypass Toggle settings to use 1-based "Plugin #" field for clarity

### Improvements
- **SDK v2 Migration**: Fully migrated to `@elgato/streamdeck` SDK v2 with `SingletonAction` class-based architecture
- **WebSocket Client**: Auto-reconnect with exponential backoff (1s → 30s max) for reliable DirectPipe connection
- **State Sync**: Real-time state broadcast — all buttons update instantly when DirectPipe state changes
- **Connection Alert**: Visual alert on all action buttons when DirectPipe is disconnected

### Actions
| Action | Description |
|--------|-------------|
| **Bypass Toggle** | Toggle individual VST plugin bypass. Long-press for master bypass. |
| **Volume Control** | Adjust input/monitor volume. Supports Keypad (press = mute) and Encoder (dial = adjust, push = mute, long-touch = reset 100%). |
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
- Auto-generated SVG → PNG icons (13 icon variants, @2x Retina support)
- Node.js 20 runtime
