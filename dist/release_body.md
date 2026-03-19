## DirectPipe v4.0.2 - Input/Panic Control Clarification + SR Consistency

> Follow-up release after v4.0.1 stability patch.
> Focus: clearer mute semantics, API/state consistency, and sample-rate-safe NR timing.

---

### Highlights

#### 1) Independent Input Mute (UI + API + state)
- Added/finished independent **Input Mute** behavior:
  - mutes microphone input only
  - keeps VST chain and output paths running
- `input_muted` is now explicitly independent from panic `muted`.
- Input mute state is available in HTTP/WS state broadcast for external controllers.

#### 2) Panic Mute behavior clarified and preserved
- Panic still acts as an emergency output-path block:
  - OUT/MON/VST paths blocked
  - active recording stops on panic engage
  - previous user mute states restore on unmute
- Panic-state exceptions are documented and preserved for maintenance/prep flows:
  - `InputMuteToggle`, `XRunReset`, `SafetyLimiterToggle`, `SetSafetyLimiterCeiling`, `AutoProcessorsAdd`

#### 3) UI semantics updated for operational clarity
- Mute-related button states refined for clearer live operation:
  - INPUT: green(active) / red(muted)
  - OUT/MON/VST: green(normal) / orange(user mute) / red(panic lock)
  - PANIC active label: `UNMUTE`
  - AUTO active: yellow with high-contrast text

#### 4) State model/API consistency
- `active_slot` unified to **0-5** (`5=Auto`, `-1` when no active slot).
- `auto_slot_active` remains for backward compatibility but is deprecated.

#### 5) Stability and DSP timing fixes
- XRun 60s window now uses real elapsed time (drift fix).
- CPU/XRun label click-to-reset added for faster troubleshooting.
- `VSTChain.isStable()` used as an additional auto-save safety guard.
- Noise Removal timing constants now recalculate from runtime sample rate:
  - hold time remains consistent across SR
  - gate smoothing remains consistent across SR

#### 6) Documentation + comment sync
- README and docs (`/docs`) updated to match actual runtime behavior.
- Control-layer comments updated so maintainers can follow panic/input-mute ownership and state responsibilities directly in code.

---

### Upgrade Notes

- **Controller/API integrators**
  - Treat `input_muted` and `muted` as separate state fields.
  - Prefer `active_slot` as source of truth (`0-5` / `-1`).
  - Keep `auto_slot_active` only as a compatibility bridge.

- **Operational behavior**
  - Panic restores pre-panic user mutes.
  - Input mute remains independent before/during/after panic.

---

### Downloads

| File | Description |
|------|-------------|
| `DirectPipe-v4.0.2-Windows.zip` | DirectPipe.exe + Receiver VST2 (`.dll`) + VST3 (`.vst3`) |
| `DirectPipe-v4.0.2-macOS.dmg` | DirectPipe.app + Receiver VST2 (`.vst`) + VST3 (`.vst3`) + AU (`.component`) |
| `DirectPipe-v4.0.2-Linux.tar.gz` | DirectPipe + Receiver VST2 (`.so`) + VST3 (`.vst3`) |
| `com.directpipe.directpipe.streamDeckPlugin` | Stream Deck plugin package |
| `checksums.sha256` | SHA-256 checksums for integrity verification |

> Synced versions: Stream Deck plugin 4.0.2.0 / DirectPipe host 4.0.2

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.1...v4.0.2
