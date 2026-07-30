# Changelog

Major notable changes to DirectPipe (maintained in this repository era, including v3.4.0+) are documented in this file.

---

## [Unreleased]

---

## [4.2.5] - 2026-07-30

### Fixed

- **Bounded zero-active recovery**: Failed main-engine and monitor-output
  recovery now re-arm the full three-second cooldown instead of being retried
  by every 30 Hz status tick.
- **Selected monitor device startup**: The monitor manager now opens the
  configured shared-mode output directly. An unavailable or exclusively held
  system default can no longer prevent a different valid monitor device from
  opening.
- **Exclusive-output transition ordering**: DirectPipe now suspends a
  potentially conflicting monitor before opening an exclusive main output,
  restores it on rollback or when the resulting main endpoint differs, and
  leaves it disabled with a notification only when both paths resolve to the
  same endpoint.
- **Windows endpoint-event feedback**: Endpoint events caused by DirectPipe's
  own forced same-device reopen are suppressed for a bounded settle window.
  Windows Audio Exclusive Mode is also classified consistently as exclusive
  during monitor-conflict checks.

### Changed

- **Release synchronization**: Host, bundled Receiver, Stream Deck metadata,
  current documentation, release notes, and test inventory are aligned to
  v4.2.5. IPC protocol v1, the 192-byte shared-memory header, presets, Stream
  Deck actions, and control payload schemas remain compatible.

### Tests

- Added regressions for failed main and monitor zero-active recovery, opening a
  selected monitor without first opening the system default, exclusive-main
  monitor preflight, Windows exclusive-driver classification, and endpoint
  event suppression. Local Windows Release CTest completed all 500
  registrations: 498 passed, 2 environment-dependent tests skipped, and 0
  failed.
- Corrected the platform-audio regression itself so macOS and Linux validate
  their native shared device types instead of applying a Windows-only
  expectation. The failed `v4.2.4` CI tag was not published as a GitHub Release;
  v4.2.5 is the immutable release tag for this update.

---

## [4.2.3] - 2026-07-29

### Fixed

- **Late plug-in PDC refresh**: DirectPipe now observes latency-change reports
  from every loaded processor and refreshes the audio graph on the message
  thread. A plug-in that publishes its active latency after being un-bypassed
  now updates the bottom `Latency:` value, WebSocket `latency_ms`, HTTP
  `/api/perf` `latencyMs`, and chain PDC fields without another device or chain
  change.
- **Redundant latency rebuilds**: Generic plug-in host-display notifications
  that carry JUCE's default latency flag no longer rebuild the graph when the
  active serial chain's reported PDC is unchanged.
- **Queued recovery lifetime**: Destroying an `AudioEngine` now invalidates
  pending message-thread recovery work even when initialization never reached
  the running state, preventing a stale callback from accessing the released
  engine.

### Changed

- **Release synchronization**: Host, bundled Receiver, Stream Deck metadata,
  current documentation, release notes, and test inventory are aligned to
  v4.2.3. IPC protocol v1, the 192-byte shared-memory header, presets, Stream
  Deck actions, and control payload schemas remain compatible.

### Tests

- Added regression coverage for late latency publication after un-bypass,
  unchanged host-display notifications, and preloaded-chain listener
  registration. The zero-active-channel recovery test now drains queued work
  after engine destruction. Local Windows Release CTest completed all 492
  registrations: 490 passed, 2 environment-dependent tests skipped, and 0
  failed.

---

## [4.2.2] - 2026-07-29

### Changed

- **Total latency reporting**: The bottom status bar, WebSocket state
  `latency_ms`, and HTTP `/api/perf` `latencyMs` now include the active plugin
  graph's reported PDC in addition to the input/output buffer estimate and
  measured callback execution time. Bypassed plugins are excluded. This remains
  an estimate, not a hardware loopback measurement.
- **Latency diagnostics**: Existing control-state `chain_pdc_samples`,
  `chain_pdc_ms`, and per-plug-in `latency_samples` fields provide the reported
  PDC breakdown behind the new total. IPC, preset, Stream Deck action, and
  control-client contracts remain compatible.
- **Documentation and maintenance rules**: Current API/state counts, hotkey
  defaults, slot filenames, Stream Deck dependency/version data, known DSP
  limitations, and source-comment guidance now match the implementation.

### Fixed

- **Initial control-state channel mode**: The default `channel_mode` snapshot now
  starts at Stereo (`2`), matching the AudioEngine default instead of briefly
  reporting Mono before the first runtime refresh.
- **DSP cleanup**: Removed unused Auto Gain smoothing coefficients and clarified
  the non-48kHz noise-removal passthrough and immediate VST bypass-tail behavior
  without changing either runtime contract.

---

## [4.2.1] - 2026-07-15

### Fixed

- **Recording lifecycle and playback state**: Recording completion is retained for direct stop and audio-device restart/loss paths. Writer publication/teardown is serialized, only accepted writer blocks count toward duration, dropped blocks are diagnosed, rapid restarts use unique filenames, and Play remains scoped to the selected/default recording folder.
- **Noise-removal latency and large blocks**: The RNNoise output FIFO is callback-size aware, seeded with the declared 480-sample delay, safe through 4096-sample callbacks, and reports zero latency for unsupported-rate passthrough.
- **VST graph lifecycle race**: Device prepare/release and message-thread graph mutations now share a `graphControlLock_ -> chainLock_` control boundary while `processBlock()` remains lock-free. Pending async replacements can be invalidated without resurrecting cleared state.
- **Reset, autosave, and slot durability**: Factory Reset always exits loading state; Clear/Reset invalidates stale loads; failed writes retain dirty state; unsafe slot switch/copy/export/rename operations abort; active-slot import keeps rollback state until load success.
- **Transactional preset and backup restore**: Every target plug-in chain is prepared before live state changes. External settings/control/audio/slot state is committed with rollback protection, and the live graph is swapped once only after the full restore is ready.
- **Backup and settings errors**: Full Backup carries recording-folder configuration, live reset/import applies it, partial device restore and settings/control write failures are surfaced, and failed file deletion is no longer reported as complete success.
- **UI callback lifetime**: Asynchronous popup-menu callbacks use component-safe lifetime guards instead of dereferencing destroyed tabs/windows.
- **MIDI learn concurrency**: Learn completion is delivered on the JUCE message thread, superseded or shutdown sessions cannot run stale callbacks, rescans preserve lifetime state, and the timeout/input boundary has a single synchronized winner.
- **Receiver restart and RT safety**: Receiver detects a replaced POSIX shared-memory object or producer generation and reconnects, mapping/unmapping and host latency notification move out of the audio callback, and unmap waits for in-flight callbacks without changing the IPC layout.
- **Control/update state**: The control API publishes the active preset. Windows in-app updater downloads for v4.2.0 and later require an exact readable `checksums.sha256` entry and matching SHA-256; manual browser downloads are outside this check. Network/API failures and repeated update checks have explicit lifecycle/error handling.

### Changed

- **Release publication order**: CI validates and builds artifacts/checksums before promoting a draft GitHub Release, preventing an assetless public latest release on build failure.
- **Windows compatibility gate**: Release packaging fails when required VST2 or ASIO SDK inputs are absent instead of publishing an artifact without advertised support.
- **Version alignment**: App, bundled Receiver, Stream Deck package, changelog, release notes, and current documentation are aligned to 4.2.1.

### Tests
- Added deterministic recorder lifecycle/drop accounting, RNNoise 128-4096 partition invariance, concurrent VST lifecycle/structure, transactional preset/backup restore, autosave retry/reset, MIDI learn generation/timeout, Receiver generation, updater integrity, and release-workflow regression coverage.
- Local Windows Release build passed for the app, Receiver VST2/VST3, and all three test executables.
- CTest registered 484 tests: core 59/59 passed; host 421 passed and 2 environment-dependent tests skipped out of 423; focused endpoint 2/2 passed; 0 failed.
- The event-signaled IPC regression passed 100/100 stress iterations after making coalesced Windows wake-ups bounded and drain-based.
- Stream Deck tests passed 5/5; its production bundle build and package validation succeeded.
- Version-only metadata, UTF-8 text integrity, JSON/YAML parsing, `git diff --check`, and SDAD v3.2.2 strict Doctor gates passed.
- Real-device audio, third-party VST crash containment, and macOS/Linux hardware checks were not run and remain separate evidence gates.

---

## [4.2.0] - 2026-07-10

### Added
- **Selected-input endpoint watcher**: Windows Core Audio property/state notifications for the selected capture endpoint now drive same-device recovery for **Audio enhancements**, **Voice Clarity**, **Voice focus**, default-format, and exclusive-mode changes even when JUCE emits no stop callback. Recovery is endpoint-event based, not silence based.
- **Dedicated endpoint tests**: Added a focused endpoint-watcher executable and CI/pre-release coverage alongside the core and host suites.
- **Stream Deck reconnect tests**: Added Node coverage for discovered-port use, stale socket cleanup, and reconnect scheduling.

### Changed
- **Monitor clock bridge and lifecycle**: MonitorOutput keeps adaptive fractional playback, bounds target fill, drains in-flight main-RT writes before ring mutation, and rejects stale recovery/reinitialize callbacks with lifecycle generations.
- **Control-server ownership**: HTTP client sockets use shared lifetime ownership and stable plugin snapshots. WebSocket clients become broadcast-visible only after handshake/initial-state completion, queued state is re-snapshotted before send, and shared connection snapshots stay alive through writes.
- **Exact backup semantics**: Settings-only and full-backup import now operate as rollback-capable transactions. Restore entry points atomically claim the single load owner and reject another active load, a partial chain, or an unstable VST transition; full restore additionally requires explicit stable-runtime proof. Full export applies the same loading/partial/unstable guard and reports active-slot, control staging, or structurally corrupt slot failures instead of overwriting a valid slot or emitting an incomplete backup.
- **Strict control schema**: WebSocket numeric/string/boolean parameters, imported optional action fields, action enums, MIDI type/CC/note/channel ranges, server ports, and boolean fields are validated before conversion or dispatch.
- **Release workflow gates**: CI checks out a requested release tag, verifies it against the canonical version, and runs Stream Deck tests/validation. Version-only checks now cover Receiver and package-lock metadata plus current/versioned release bodies.
- **Stream Deck dependency security refresh**: Updated the production `ws` dependency to 8.21.0 and refreshed the transitive lockfile; full and production-only `npm audit` now report 0 vulnerabilities.

### Fixed
- **Manual output mute preservation**: Manual and automatic device-loss mute ownership are tracked separately. Endpoint/panic recovery no longer clears user mute intent, while clearing manual mute cannot bypass an active automatic loss mute.
- **Stale monitor callbacks**: Queued fallback/recovery and main-device restart callbacks can no longer close or revert a newly selected monitor device.
- **RT teardown races**: Monitor and SharedMemWriter close admission and drain in-flight writes before resetting ring memory or unmapping shared memory.
- **Preset structural recovery**: Parseable corrupt primaries such as `{}`, malformed plugin entries, or non-canonical Base64 state no longer overwrite destinations or block recovery from valid `.bak`/legacy backups; a valid backup can still load if primary self-repair copying fails. Legacy numeric slot families whose only surviving file is `.bak` or `.backup` migrate to the canonical letter slot, so occupied slots remain loadable and are included in full backups. Startup autosave recovery also honors an explicit `outputMuted` value from a backup-only or locked-primary family, including partial plugin loads.
- **Settings rollback gap**: A late control-config save failure no longer leaves already-applied audio settings behind.
- **Control mapping defaults**: Invalid persisted actions no longer silently become `PanicMute`, and invalid MIDI enums no longer create dead mappings.
- **Updater transaction and command safety**: The Windows updater stages replacements, preserves/rolls back a known-good executable, escapes literal percent paths, rejects non-strict release tags, and waits only for the exact launching DirectPipe PID. Finished/failed download workers are reaped so a later update attempt can retry, and a lifecycle mutex serializes reap/start/destruction around the reusable `std::thread`.
- **WebSocket dead-client accumulation**: Close/read-error/failed-handshake exits close their sockets and are swept even while no state broadcasts occur, so idle dead clients cannot consume the client limit.
- **Device state reporting**: Directional input/output losses now report `InputLost`/`OutputLost` instead of being masked as `BothLost` by the generic loss flag. Output None selection or manual output-loss clearing publishes direction-first and rechecks/re-arms recovery, so a concurrent input loss cannot be cleared from aggregate `deviceLost` state.
- **Stream Deck discovery/reconnect**: The host announces the actual WebSocket port immediately and every 2 seconds; the plugin binds UDP discovery before its first WebSocket attempt, uses the discovered port, and clears stale socket state across close/error paths.

### Upgrade
- For **Windows v4.1.2 → v4.2.0**, use the GitHub Windows ZIP once. The new transactional updater protects updates initiated by v4.2.0 and later, but cannot change updater code already installed in v4.1.2.
- Receiver VST replacement is not required for these host-side fixes; the control API and Receiver IPC protocol remain compatible.
- Update the Stream Deck plugin to 4.2.0 for discovery/reconnect fixes. GitHub provides the manual package; Marketplace publication remains a separate submission step.

### Tests
- Release build completed for the app, Receiver VST2/VST3, and all test targets.
- `directpipe-tests`: 52/52 passed.
- `directpipe-host-tests`: 391 total, 389 passed, 2 environment-dependent skips.
- `directpipe-endpoint-watcher-tests`: 2/2 passed.
- CTest: 445 registered, 0 failures, 2 environment-dependent skips.
- Stream Deck tests: 5/5 passed; build and package validation passed.
- Focused endpoint automation passed; real-device **Audio enhancements**, **Voice Clarity**, **Voice focus**, and default-format changes remain an unexecuted manual field check for this local run.

---

## [4.1.2] - 2026-07-07

### Fixed
- **Device-change input loss guard**: External device stops/errors now immediately mark input lost, auto-mute output, and clear meters; reconnection no longer accepts a fallback/current device while `inputDeviceLost_` is waiting for the saved input target. Same-device Windows endpoint restarts, such as microphone enhancement toggles, keep the loss state until DirectPipe re-opens the same device setup, preventing wrong-input capture, silent receive, or buzz-like audio after device changes.
- **Immediate retry on device-list changes**: Device-list notifications now bypass the 3-second reconnect polling cooldown, so a returning microphone/output endpoint is retried immediately instead of waiting for the next timer tick.
- **Manual device-toggle workaround removal**: Same-device restarts now force a same-setup re-open after a short settle delay, replacing the previous workaround of switching to another input device and switching back.
- **Per-direction manual device selection**: Manual input selection no longer clears pending output loss/auto-mute, and manual output selection no longer clears pending input loss. ASIO/full driver selection still clears both directions only after the combined device switch succeeds.
- **Stale reconnect callback guard**: Pending delayed same-device re-open callbacks are generation-guarded and canceled when the user changes driver/device type or ASIO device, so an old Windows endpoint-restart timer cannot reopen a superseded setup.
- **Intentional-change reconnect race**: Immediate reconnect from device-list notifications is ignored while an intentional device reconfiguration is in progress.
- **Strict ASIO startup restore**: Saved ASIO startup restore now treats ASIO as one duplex driver. Mismatched saved input/output names are normalized to one ASIO target, saved ASIO targets no longer fall through to other ASIO drivers such as FL Studio ASIO or Realtek ASIO when the saved device is missing, and ASIO SR/BS/channel-mask restore is delayed until that saved ASIO device is actually active.
- **Plugin-chain partial restore detection**: Preset and settings imports now report failure when a requested VST entry cannot be loaded, and malformed plugin-chain JSON is rejected before audio/slot state is applied, so incomplete plugin-chain restores arm the partial-load guard instead of being treated as clean loads.
- **Settings/full-backup restore prevalidation**: Settings-only and full-backup imports validate audio, control, and slot payload shape before applying dependent sections, reducing half-applied restore states after invalid backup content.
- **Full-backup slot rollback**: Full-backup slot-file restore now snapshots existing slot files and rolls them back if the exact slot clear/write phase fails, preventing partially updated quick-slot files after file-system errors.
- **Preload shutdown join cleanup**: Plugin preload cancellation now moves background work onto shared state with bounded message-thread shutdown cleanup, avoiding indefinite waits or owner lifetime hazards if a preload is stuck.
- **Windows auto-start command quoting**: The HKCU Run entry now stores the executable path as a quoted command, so installs under paths with spaces such as `Program Files` start correctly after reboot.

### Release
- **Version metadata sync**: App, bundled Receiver plugin metadata, Stream Deck manifest/package metadata, README, user guide, product spec, architecture notes, log rules, building docs, changelog, and release body are aligned to v4.1.2.
- **Host-side hotfix scope**: v4.1.2 is documented as a host-side audio-device recovery and save/restore hardening hotfix. Users do not need to reinstall Receiver VST only because of this release.

### Tests
- Added regression coverage for input-loss retry protection, external device stop/error muting, same-device endpoint restarts, cooldown bypass on device-list changes, per-direction manual device-loss clearing, strict ASIO duplex restore, malformed/incomplete plugin-chain import reporting, partial-load autosave guards, backup prevalidation, full-backup slot rollback, and quoted Windows auto-start commands.
- Rebuilt Release app, Receiver, and test targets, then ran core/host validation: `directpipe-tests` 52 passed; `directpipe-host-tests` 329 tests, 327 passed, 2 environment-dependent tests skipped; `ctest` 381 registered, 0 failed, 2 skipped.

---

## [4.1.1] - 2026-07-03

### Fixed
- **Preset slot cache crash (#4)**: Slot-cache hits now compare the cached plugin chain against the saved slot file by plugin type/name/path before swapping cached instances into the active chain. Duplicate VST entries with different saved state no longer reuse an incompatible cached chain.
- **Partial cache-swap failure recovery**: Failed cached-instance swaps leave the currently active VST chain intact and fall back to a cold slot load instead of leaving a partially moved plugin chain behind.
- **Preset state preservation on cache fallback**: Cache-hit state refresh now copies fresh slot-file state into preloaded requests instead of moving it out before the cached graph swap succeeds, so cold fallback keeps the saved plugin state intact.
- **Settings-only backup shape**: Settings-only `.dpbackup` exports now strip slot-chain data and `activeSlot`, so importing settings cannot silently overwrite presets or jump the active slot.
- **Backup import failure reporting**: Settings and full-backup imports now propagate failed audio-settings imports to the caller instead of silently reporting success after a partial restore.
- **Full backup restore exactness**: Full restore now saves the active slot before export, falls back to atomic-write backup files while importing, deletes slots that are missing from the backup, clears stale `.bak`/`.backup`/`.tmp` and legacy numeric slot families, and refreshes slot occupancy/names/preload cache after restore.
- **Audio restore hardening**: Startup/tray restore keeps staged desired device state across invalid driver callbacks, preserves requested ASIO sample rate during restore, only clears startup restore pending after the saved targets are actually active, and recovers from zero-active-channel or mismatched explicit-channel starts without requiring manual device toggles.

### Release
- **Version metadata sync**: App, bundled Receiver plugin, Stream Deck manifest/package metadata, README, user guide, product spec, architecture notes, log rules, building docs, changelog, and release body are aligned to v4.1.1.
- **Host-side hotfix scope**: v4.1.1 is documented as a host-side hotfix. Users do not need to reinstall Receiver VST only because of this release.

### Tests
- Added regression coverage for duplicate-plugin preset cache validation, fresh-state preservation on cache fallback, settings-only backup import shape, import failure propagation, full-backup exact slot restore, backup-family cleanup, backup-fallback import, zero-active-channel recovery, explicit-channel readiness, zero-active-monitor-output guards, and ASIO sample-rate restore preservation.
- Rebuilt Release targets and ran full core/host validation: `directpipe-tests` 52 passed; `directpipe-host-tests` 310 tests, 308 passed, 2 environment-dependent tests skipped; `ctest` 362 registered, 0 failed, 2 skipped.

---

## [4.1.0] - 2026-06-21

### Changed
- **Adaptive PLL monitor bridge**: Separate monitor output now uses fractional read playback with linear interpolation and an adaptive PLL controller to absorb small independent-device clock drift. Normal drift correction no longer discards frames.
- **Runtime-derived monitor target**: Monitor target fill is derived from producer block size, monitor consumer block size, sample rate, ring capacity, and recent underrun/stability behavior. Repeated underruns raise the target gradually; stable fill lowers it toward the runtime-derived minimum.
- **Emergency trim fallback only**: Old-frame trim is now reserved for near-overflow safety fallback and still uses a short fade-in after emergency trims.
- **Monitor audit snapshots**: Audit Mode `MONITOR` lines now include PLL/fill fields such as `fillFrames`, `targetFill`, `targetReason`, `playbackRatio`, `pllErrorFrames`, `pllErrorMs`, `pllCorrection`, `driftEstimate`, `priming`, `emergencyTrimDelta`, `underrunDelta`, `droppedFramesDelta`, block sizes, sample rate, and ring capacity.
- **Inherited release-prep notes**: This is the next public release after v4.0.9, so the startup/tray device-restore, XRun-window, and broader Audit Mode work prepared after v4.0.9 is included in v4.1.0.

### Fixed
- **Startup/tray WASAPI restore**: Settings restore now remembers the saved input/output device targets even when Windows has not finished enumerating devices at login. DirectPipe keeps waiting for the saved devices instead of accepting an unintended fallback device as success.
- **Small monitor buffer crackle/cutout**: The monitor callback avoids repeated partial audio plus zero-fill and uses adaptive fractional playback instead of normal drift frame drops, reducing the need to raise the monitor buffer above the device minimum.
- **Runtime monitor block-size changes**: When the main producer block size or monitor consumer block size changes, MonitorOutput re-enters priming so the newly derived target fill is honored before playback resumes.
- **Main OUT protection**: Monitor output remains post-VST/post-Safety, does not run the VST chain twice, and stays below main OUT scheduling priority.
- **Monitor callback scheduling on Windows**: The separate monitor WASAPI callback thread now registers with MMCSS `Pro Audio` at normal priority instead of critical priority, so monitor output does not preempt the main OUT callback and inflate OUT XRuns.
- **MMCSS lifecycle contract**: Main OUT and monitor AvRT cleanup now avoids cross-thread `AvRevertMmThreadCharacteristics` calls and publishes cached AvRT function pointers before allowing RT registration.
- **Inactive monitor routing cost**: The main audio callback now skips monitor scaling/copy work unless the separate monitor output is actually `Active`, reducing RT work during monitor device loss, sample-rate mismatch, or reconnect.
- **Inactive monitor meter reset**: Monitor RMS level is cleared immediately when monitor routing is disabled or inactive, so Audit Mode and UI meters do not report stale monitor audio.
- **XRun display over-reporting after UI stalls**: The 60-second XRun rolling window now advances every elapsed bucket when the message thread is delayed by device restart/restore work, so stale XRuns age out on time instead of lingering for minutes.
- **Audit XRun delta accuracy**: Audit Mode `xrunDelta` now comes from a monotonic XRun event counter instead of comparing 60-second rolling totals, preventing stale bucket aging from appearing as new XRuns.

### Diagnostics
- **Wider Audit Mode audio snapshots**: Audit Mode now emits rate-limited `AUDIO` and `MONITOR` status snapshots from the message thread, including desired/actual devices, SR/BS, CPU/processing time, XRUN and callback-overrun deltas, monitor dropped/underrun/trim deltas, and mute/lost/IPC/limiter flags.

### Release
- **Version metadata sync**: App, bundled Receiver plugin metadata, Stream Deck manifest/package metadata, README, user guide, product spec, architecture notes, log rules, building docs, and release body are aligned to v4.1.0. Existing Receiver VST installs do not need manual replacement for these host-side fixes.
- **Platform support wording sync**: Release docs now consistently distinguish Windows as the stable, locally verified v4.1.0 target from macOS beta and Linux experimental CI/source-supported builds. Stream Deck support is documented as a separate cross-platform package.

### Tests
- Added fractional ring-buffer interpolation coverage, adaptive PLL/target policy coverage, MonitorOutput callback state regression coverage, startup restore retry coverage, inactive monitor meter reset coverage, and bounded monitor drift regression coverage.
- Rebuilt the Release app/plugin/test targets and reran full core/host test validation.

---

## [4.0.9] - 2026-06-20

### Fixed
- **Monitor drift trim over-correction**: The v4.0.8 monitor latency drift trim now accounts for both the main audio callback producer block size and the monitor-device consumer block size. This prevents the monitor ring buffer from being trimmed below the main callback granularity when the monitor buffer is smaller than the main buffer, reducing dropouts without reverting the long-session drift protection.
- **Monitor behavior clarification**: Confirmed the monitor path still receives the already processed post-VST/post-Safety audio buffer; no second VST processing pass is performed for monitor output.
- **Defensive audio buffer edge handling**: `AudioRingBuffer`, `OutputRouter`, `MonitorOutput`, `SharedMemWriter`, and `AudioRecorder` now guard zero-channel, null-channel, or short-source-buffer calls so edge-case silence paths do not reuse stale audio or read past the source buffer.
- **Preset slot switch crash guard**: Slot-cache hits now verify the cached plugin chain against the current slot file by plugin type/name/path before swapping instances, preventing stale preload entries from being reused after reset, restore, delete, or import operations.
- **Factory Reset slot cleanup**: Factory Reset and Clear All Presets now clear in-memory slot names, occupancy state, pending slot loads, and the preload cache immediately after destructive slot cleanup, so deleted slot names do not remain visible.
- **Preset backup cleanup**: Preset reset/delete/copy/import/export paths now consistently remove `.bak`, `.backup`, `.tmp`, and legacy numeric slot files, preventing stale atomic-write backups from reviving old settings or slot names.

### Tests
- Added `MonitorDriftPolicyTest`, `AudioRingBufferTest`, and `OutputRouterTest` coverage for small-monitor-buffer, large-producer-block, zero-channel, null-channel, and short-source-buffer cases.
- Rebuilt and ran targeted audio routing/drift tests, targeted preset/settings tests, and the full host test suite for the slot reset cleanup and audio edge-case paths.

---

## [4.0.8] - 2026-06-12

### Fixed
- **Full Restore audio refresh**: Full backup/preset restore now reopens the current audio device when the driver reports an invalid stopped state such as `SR=0`, so ASIO and WASAPI restores can recover audio without requiring a second restore or manual buffer/device change.
- **Monitor latency drift compensation**: Separate monitor output now trims stale ring-buffer frames when independent device clocks drift apart, keeping macOS/CoreAudio monitoring latency bounded instead of letting it grow until monitoring is toggled (#3).
- **Invalid audio device state guards**: Device start, driver switch, sample-rate apply, and IPC enable paths now reject invalid `SR=0`/buffer state before it can overwrite current or desired runtime audio settings.
- **Restore-time device loss state**: Channel mask restore now applies device setup through AudioEngine's intentional-change path, preventing normal restore stop/start cycles from being mistaken for external device loss.
- **Uninitialized driver type changes**: Setting a driver type before `AudioEngine::initialize()` now records intent without opening a real audio device or leaking callbacks into teardown.

---

## [4.0.7] - 2026-05-26

### Added
- **Tray Panic Mute toggle**: The tray/menu bar right-click menu now includes a checked `Panic Mute` item directly under `Show Window`, using the same restore-safe Panic Mute path as the main UI button.

### Changed
- **Version and release docs sync**: Updated app, Receiver plugin, Stream Deck plugin, README, user guide, architecture/spec docs, and release body references for v4.0.7.

### Fixed
- **Monitor output sample-rate mismatch freeze**: Monitor sample-rate mismatch is now treated as a disabled configuration state instead of a retryable device-loss state, preventing the UI message thread from entering a tight monitor reconnection loop. Fixes [#2](https://github.com/LiveTrack-X/DirectPipe/issues/2).
- **Monitor reconnection success reporting**: Monitor reconnection success logs and notifications now require the monitor output to actually become `Active`, avoiding false "reconnected" messages after a mismatch.
- **Monitor teardown loss noise**: Internal monitor restart/shutdown no longer reports its own teardown as an external device-lost event.
- **Sample-rate apply state alignment**: When an audio device applies a different sample rate than requested, DirectPipe now aligns desired/current sample-rate state to the actual runtime value instead of keeping a stale requested value.
- **Failed settings apply guards**: Failed driver/device/channel/monitor setting changes no longer mark settings dirty or overwrite desired runtime targets, preventing invalid selections from being saved or retried later.

---

## [4.0.6] - 2026-05-20

### Added
- **Panic Mute tray/menu bar indicator**: When Panic Mute is active, the tray/menu bar icon now shows a red slash overlay so the emergency muted state is visible even while the main window is hidden.

### Changed
- **Stream Deck update load reduction**: Split control-state broadcasts from high-frequency telemetry and throttle telemetry updates, reducing Stream Deck UI traffic while keeping control changes immediate.
- **Stream Deck render caching**: Added action render caching to avoid repeated SDK image/title updates when state has not actually changed.

### Fixed
- **Stream Deck Panic Mute visual state**: Panic Mute now forces the matching Stream Deck key image/title so muted and unmuted states do not appear inverted after Stream Deck profile cache refreshes.
- **Panic Mute restore path**: Host-side restore now tracks pending Panic Mute restoration separately from the engine mute flag, so explicit Stream Deck unmute commands can restore the previous output/monitor/IPC state on the first command.

### Known Issues / 확인 중
- **Full Restore runtime refresh gap / 전체 복원 후 런타임 갱신 누락 의심**: After importing a full backup, restored VST/IPC processing may not become active until the audio device is restarted, for example by changing buffer size or reselecting the device. This appears to be a restore-time runtime refresh issue when the saved device setup matches the current device setup. 전체 백업 복원 후 저장된 장치 설정이 현재 설정과 같으면 오디오 장치 재시작이 생략되어 VST/IPC 런타임이 바로 준비되지 않을 수 있습니다. 임시 우회는 버퍼 사이즈 변경 또는 장치 재선택입니다. v4.0.8에서 해결됨 / Fixed in v4.0.8.

---

## [4.0.3] - 2026-03-22

### Added
- **Startup behavior option (tray launch)**: Added `Start Minimized to Tray` toggle to both Settings > Application and tray right-click menu, backed by `settings.dppreset` persistence so both entry points stay in sync.

### Changed
- **Auto-start label (cross-platform UI consistency)**: Unified tray menu and Settings toggle to use a single platform label source via `Platform::getAutoStartLabel()` (`Open at Login` on macOS, `Start with System` on others), removing Windows-specific hardcoded wording from the Settings toggle path.

### Fixed
- **Global Safety Guard runtime (legacy SafetyLimiter naming)**: Global stage now runs zero-latency sample-peak guard behavior (stereo-linked, instant attack, release smoothing, final hard clamp) with legacy class/API/action names preserved for compatibility.
- **BuiltinAutoGain post limiter**: Added constant-latency post limiter after AGC (default `-1.0 dBTP`, fixed internal `1.0ms` lookahead + `50ms` release, constant delayed path, hard clamp). Advanced UI exposes `Limiter Ceiling` only.
- **ASIO channel selection reset on driver/device switch**: Preserved per-driver input/output channel masks in `DriverTypeSnapshot`, and persisted explicit `inputChannelMask`/`outputChannelMask` index arrays in preset JSON for restart-safe restore (including non-contiguous routing). Invalid saved indices now fall back to safe defaults.

---

## [4.0.2] - 2026-03-19

### Added
- **Input Mute**: Independent input mute — silences microphone while VST chain continues processing (reverb tails fade naturally, AGC enters freeze). [INPUT] button in INPUT section, green=active / red=muted
- **VSTChain.isStable()**: Additional auto-save safety net
- **XRun**: Click CPU/XRun label to manually reset counter

### Changed
- **State Model (breaking)**: `active_slot` now 0-5 (5=Auto). `auto_slot_active` deprecated
- **Input Mute Toggle**: Now independent from Panic Mute
- **`input_muted` state field**: Now independent from `muted`
- **Mute Button UX**: INPUT/OUT/MON/VST/PANIC/AUTO button colors and labels now clearly distinguish normal/user-mute/panic-lock states (PANIC active shows `UNMUTE`)

### Fixed
- **XRun**: 60-second window drift — now uses real elapsed time
- **Panic**: Shows notification when recording was stopped during panic mute
- **UI**: Freeze Level label LUFS → dBFS, AGC labels clarified (Boost/Cut)
- **Noise Removal**: `holdSamples` and `gateSmooth` are now recalculated from runtime sample rate to preserve intended timing across SR changes

---

## [4.0.1] - 2026-03-19

### Fixed
- **NoiseRemoval**: Ring buffer uint32_t overflow causing permanent silence after ~25h continuous use
- **HTTP API**: Strict numeric validation — reject mixed alpha-numeric input (e.g., "abc0.5")
- **UI**: Plugin chain editor negative height on very small window
- **State Broadcast**: activeSlot clamped to 0-4, added `auto_slot_active` field for WebSocket/SD clients
- **Linux**: Complete XDG Desktop Entry Exec key character escaping per spec §6
- **macOS**: Notify user when hotkey accessibility permission not granted
- **Linux**: Show "unsupported" message in Hotkeys tab instead of non-functional UI
- **HTTP API**: Escape JSON special characters in API responses
- **Platform**: AutoStart setters return bool, notify user on failure
- **IPC**: Restrict POSIX semaphore/shm permissions to owner-only (0600)
- **XRun Tracking**: Device restart no longer clears XRun history — display persists for full 60s window
- **Audio RT**: Moved MMCSS LoadLibraryA call from RT callback to audioDeviceAboutToStart
- **MIDI**: Fixed LearnTimeout use-after-free when timer callback destroyed itself
- **Preset**: Added loadingSlot_ guard to Reset Auto to prevent intermediate state auto-save
- **Audio**: Recorder now stops on device loss (audioDeviceStopped) to prevent WAV corruption
- **Preset**: Auto first-click failure now sets partialLoad_ to prevent saving incomplete chain
- **Preset**: pendingSlot_ cleared on Factory Reset / Clear All Presets
- **State**: Plugin name now included in quickStateHash — name changes trigger WebSocket broadcast
- **WebSocket**: broadcastToClients releases clientsMutex_ before socket writes — prevents slow client blocking shutdown
- **IPC**: Receiver VST now calls detach() before close() on failed producer check — fixes spurious multi-consumer warning
- **Thread Safety**: audioDeviceAboutToStart non-atomic variable writes deferred to message thread via callAsync
- **Update**: PowerShell batch script now escapes single quotes in paths (e.g., O'Brien user profiles)
- **Audio**: Added null guard on outputChannelData for ASIO legacy drivers
- **Preset**: parseSlotFile now uses .bak fallback (loadFileWithBackupFallback) for crash resilience
- **UI**: Plugin removal now identifies by index+name, not name-only (fixes duplicate-name removal)
- **WebSocket**: Sends RFC 6455 close frame (1009) before disconnecting oversized-message clients
- **Settings**: importAll/importFullBackup now check platform compatibility internally
- **Platform**: macOS/Linux AutoStart uses atomicWriteFile for crash-safe plist/desktop writes
- **MIDI**: Guard LearnTimeout callAsync with alive_ flag — prevent use-after-free on rapid shutdown
- **Audio RT**: Add release/acquire barrier for MMCSS cached function pointers (ARM memory ordering safety)
- **Audio RT**: Guard partial GetProcAddress failure — null both pointers to allow retry
- **VST**: Validate nodeId in openPluginEditor second lock scope — prevent TOCTOU use-after-free
- **HTTP**: Close client sockets on HttpApiServer::stop() — prevent shutdown hang on blocked handlers
- **WebSocket**: Close socket on handshake failure — prevent zombie connections in clients_ vector
- **Cache**: Remove dangerous thread detach in PluginPreloadCache::cancelAndWait — leak instead of use-after-free
- **IPC**: Add atomic detach guard to RingBuffer — prevent null deref on concurrent detach/write
- **Audio**: Protect desiredDevice_ reads in audioDeviceAboutToStart with SpinLock (cross-thread safety)
- **Settings**: Guard SettingsAutosaver callAsync with alive_ flag — prevent use-after-free on fast quit
- **State**: Fix masterBypassed computation — no longer reports true when plugins are unloaded (DLL missing)
- **WebSocket**: Handle continuation frame (opcode 0x0) — prevent stream corruption on fragmented messages
- **Audio**: Log error when driver snapshot restore fails (setAudioDeviceSetup return value was ignored)
- **Preset**: Sync loadSlot path now sets partialLoad_ — prevent auto-save overwriting incomplete chains
- **WebSocket**: Check sendFrame return on initial state delivery — disconnect on failure
- **Preset**: Allow clearing slot name to empty (previously ghost name persisted in JSON)
- **Settings**: Reset activeSlot to A on .dpbackup import (slot files not included in backup)
- **Receiver**: Guard against processBlock before prepareToPlay — prevent infinite loop on empty buffer
- **Receiver**: Reset lastOutputSamples_ in releaseResources — prevent stale fade-out data
- **Receiver**: Add isBusesLayoutSupported for mono — fix OBS mono source left-channel-only output
- **Receiver**: Report latency via setLatencySamples — enable DAW latency compensation
- **Receiver**: Detect stale consumer_active after OBS crash — suppress false multi-consumer warning
- **Receiver**: Add drift compensation hysteresis — reduce micro-gaps at Ultra Low preset
- **Receiver**: Use saved buffer data in fade-out — eliminate click artifact on underrun
- **Stream Deck**: Enable autoReconnect — recover from host restarts without UDP
- **Stream Deck**: Clear stale state on reconnect — prevent displaying previous session data
- **Stream Deck**: Handle activeSlot -1 and auto_slot_active — correct preset display
- **Stream Deck**: Per-target volume override check — prevent unrelated targets from blocking
- **Build**: Guard RNNoise x86 sources for ARM compatibility — fix Universal macOS build
- **Build**: Conditional VST2 target in pre-release test — handle missing VST2 SDK
- **Build**: Remove stale DeviceSelector.cpp/h files
- **Audio**: Catch plugin processBlock exceptions — prevent app crash from third-party VST bugs
- **Audio**: Add 48kHz warning when Auto button clicked at non-48kHz sample rate
- **MIDI**: Overwrite duplicate CC bindings instead of creating duplicates
- **Audio**: Notify user when monitor sample rate mismatches main device
- **Audio**: Reset chainCrashed_ flag on device restart — user can recover after removing crashing plugin
- **Audio**: Revert MMCSS registration in audioDeviceStopped — prevent handle leak on device restart
- **Audio**: Protect audit log desiredDevice_ reads with SpinLock
- **Preset**: Fix onResetSettings loadingSlot_ timing — let loadFromFile manage the guard internally
- **Settings**: Clear partialLoad_ on successful self-heal in loadFromFile
- **State**: masterBypassed requires at least one loaded plugin — unloaded slots no longer trigger false bypass
- **Audio**: Windows SEH crash guard (__try/__except) for VST processBlock — catches access violations that try/catch misses
- **Audio**: chainCrashed_ now resets on chain modification (plugin add/remove), not on every device restart
- **Audio**: setInputDevice clears outputAutoMuted_ — prevents permanently stuck muted output after input device change
- **Audio**: mmcssTaskHandle_ is now std::atomic<HANDLE> — prevents data race between RT and device threads on ARM
- **Audio**: Guard SharedMemWriter::shutdown() against double call in AudioEngine::shutdown()
- **Settings**: Add null guard on getDynamicObject() in ControlMapping::load() — prevent crash on corrupt config
- **Audio**: Guard audioDeviceAboutToStart against BS=0 or SR=0 — prevent VST prepareToPlay with invalid params
- **Audio**: Move crash notification from RT thread to 30Hz message-thread timer — eliminate heap alloc in crash handler
- **Action**: Add loadingSlot_ guard to AutoProcessorsAdd fallback path — prevent intermediate state auto-save
- **File I/O**: Check atomicWriteFile return values at 4 sites — log warning on write failure instead of silent data loss
- **Security**: HTTP server enforces 64-handler connection limit — prevents DoS via connection flooding
- **Security**: WebSocket 32-client limit with atomic check+increment under clientsMutex_ — fixes TOCTOU race
- **AGC**: Internal LUFS offset increased from -4dB to -6dB — output level closer to commercial levelers

---

## [4.0.0] — Cross-Platform Release

> **v3에서 아키텍처 리팩토링 + 크로스플랫폼 확장 + 내장 프로세서 + 외부 제어 강화.**
> MainComponent를 7개 focused module로 분할, Platform/ 추상화 레이어 도입, 294+ 테스트.
>
> **Architecture refactoring + cross-platform + built-in processors + enhanced external control.**
> MainComponent split into 7 focused modules, Platform/ abstraction layer, 294+ tests.

### Added — 새 기능

- **Cross-platform support** — macOS (beta), Linux (experimental) alongside Windows (stable) / 크로스 플랫폼: macOS(베타), Linux(실험적)
- **Platform abstraction layer** (`Platform/`) — PlatformAudio, AutoStart, ProcessPriority, MultiInstanceLock with per-OS implementations / 플랫폼 추상화 레이어
- **Built-in Processors (내장 프로세서)** — VST 없이 기본 마이크 처리 제공:
  - **Filter** — HPF (60Hz ON) + LPF (16kHz OFF), 프리셋 + Custom 슬라이더 / High-pass + Low-pass filter
  - **Noise Removal** — RNNoise AI 노이즈 제거, 3단계 강도 (Light/Standard/Aggressive), 48kHz 전용, VAD 게이팅 / AI noise suppression with VAD gating
  - **Auto Gain** — LUFS 기반 자동 볼륨 레벨러, WebRTC 듀얼 envelope 패턴 / LUFS-based AGC with WebRTC dual-envelope
- **[Auto] 프리셋 슬롯** — A-E와 별도인 특수 슬롯 (인덱스 5). 원클릭 Filter+NR+AGC 기본 체인 / Special preset slot for one-click built-in processor chain
- **Safety Limiter** — VST 체인 후 모든 출력 경로 전에 적용되는 피드포워드 리미터 (0.1ms attack, 50ms release, -0.3dBFS) / Feed-forward limiter after VST chain
- **Stream Deck 10 액션** — 기존 7개 + Performance Monitor, Plugin Parameter (SD+), Preset Bar (SD+) / 10 actions (was 7)
- **19 통합 액션** — XRunReset, SafetyLimiterToggle, SetSafetyLimiterCeiling, AutoProcessorsAdd 추가 / 19 actions (was 15)
- **IPC consumer_active 감지** — Receiver VST 다중 연결 시 경고 표시 / SPSC violation warning when multiple Receivers connect
- **DPC Latency 대책** — main MMCSS "Pro Audio" AVRT_PRIORITY_CRITICAL, IPC SetEvent 최적화, 콜백 오버런 감지 / MMCSS registration, IPC optimization, callback overrun detection
- **SHA-256 체크섬 검증** — 자동 업데이터 다운로드 무결성 확인 (`checksums.sha256`) / Auto-updater integrity check
- **48kHz NotificationBar 경고** — Auto/NR 추가 시 비-48kHz 샘플레이트 경고 / Warning when NR added at non-48kHz
- **ActionHandler** — 중앙 액션 라우팅, MainComponent에서 추출 / Centralized action routing
- **SettingsAutosaver** — dirty-flag + 1초 디바운스 자동 저장 / Auto-save with debounce
- **StatusUpdater** — 30Hz UI 상태 갱신, MainComponent에서 추출 / Periodic status updates
- **PresetSlotBar** — 프리셋 슬롯 A-E 버튼, 우클릭 컨텍스트 메뉴 / Preset slot buttons with context menu
- **UpdateChecker** — GitHub API 업데이트 체크 + 다이얼로그 / Update check + dialog
- **HotkeyTab / MidiTab / StreamDeckTab** — Controls 서브탭 분리 / Split into separate files
- **ActionResult** 패턴 — ok/fail 구조화 반환값 / Structured error handling
- **HTTP CORS preflight** — 브라우저 클라이언트용 OPTIONS 핸들러 / Browser client support
- **CONTRIBUTING.md / SECURITY.md** — 기여 가이드 + 보안 취약점 보고 절차 / Contributor and security guidelines
- **294+ 테스트** — NR, AGC, VSTChain, DeviceState, PresetManager, Safety Limiter 등 / Comprehensive test suite

### Fixed — 버그 수정

- **AGC freeze gate** — 무음 구간에서 게인을 0dB로 리셋하던 버그 → 현재 게인 유지 (hold) / Freeze now holds current gain instead of resetting to unity
- **AGC 오버슈트** — 1.5s LUFS 윈도우 + IIR envelope 이중 지연 → WebRTC 듀얼 envelope + direct gain으로 해결 / Dual-envelope resolves overshoot from double-smoothing
- **Mono 합산 +3dB** — L+R 합산 시 나눗셈 누락 → numInputChannels로 평균 / Fixed mono summing to average instead of raw sum
- **MIDI Continuous 게인** — InputGainAdjust에 0-1.0 delta 적용되던 버그 → SetVolume으로 절대 게인 설정 / Continuous CC now maps to absolute gain via SetVolume
- **HTTP gain 범위** — `/api/gain/:delta` 범위 검증 추가 (-2.0~2.0) / Added range validation
- **outputFifoWrite/Read 오버플로** — BuiltinNoiseRemoval의 `int` → `uint32_t` (12시간 후 정의되지 않은 동작 방지) / Prevent undefined behavior after ~12h
- **quickStateHash 누락** — chainPDCMs 필드 미포함 → 해당 변경이 브로드캐스트 안 되던 문제 / Added missing field to state hash
- **Panic mute 모든 액션 차단** — 패닉 뮤트 중 바이패스, 볼륨, 프리셋 등 모든 액션 차단 / All actions blocked during panic mute
- **Panic mute 녹음 중지** — 패닉 뮤트 시 녹음 자동 중지 (해제 시 재시작 안 함) / Recording stops on panic engage
- **HTTP gain delta 스케일링** — `*0.1f` 보상용 `*10.0f` 스케일링 문서화 / Documented gain scaling convention

### Changed — 변경사항

- **AGC 아키텍처** — IIR envelope → WebRTC 듀얼 envelope (fast 10ms/200ms + slow 0.4s LUFS) + direct gain / Architecture rewrite
- **AGC 기본값** — hiCorr 0.75→0.90, maxGain 24→22dB, LUFS 1.5s→0.4s, release 700→100ms, -4dB 타겟 오프셋 / Updated defaults
- **CI/CD 릴리스 에셋** — `win64.zip` → `Windows.zip`, `linux-x64.tar.gz` → `Linux.tar.gz` (업데이터 플랫폼 태그 일치) / Asset names match updater
- **CI checksums.sha256** — 릴리스 아티팩트 SHA-256 체크섬 자동 생성 / Auto-generated checksums
- **MainComponent** — ~1,835줄 → ~729줄 (7개 클래스 추출) / Reduced via extraction
- **기본 핫키** — 11개 (Ctrl+Shift+F6=Input Mute, Ctrl+Shift+1~3=Plugin 1-3 Bypass 등) / 11 default hotkeys
- **juce_cryptography** 모듈 추가 — SHA-256 검증용 / Added for checksum verification

---

## [3.10.3] — Final Stable Release

> **이 버전은 v3.10.x 라인의 최종 안정 릴리스입니다 (Windows 전용).**
> 이후 치명적 버그와 보안 패치만 적용됩니다. 새 기능과 크로스 플랫폼은 v4.0+을 참조하세요.
>
> **This is the final stable release of the v3.10.x line (Windows only).**
> Only critical bug fixes and security patches will be applied after this point.
> For new features and cross-platform support, see v4.0+.

### Fixed
- `outputPanelPtr_` 댕글링 포인터 위험 — `SafePointer<OutputPanel>`로 교체 / Dangling pointer risk — replaced with `SafePointer<OutputPanel>`
- 슬롯 이름 변경 다이얼로그 AlertWindow 이중 해제 — `enterModalState` 소유권 수정 / AlertWindow double-delete in slot rename dialog — fixed `enterModalState` ownership
- pre-release-test.sh 테스트 exe 경로 수정 + summary 렌더링 버그 수정 / Fixed test exe paths and summary rendering bug

### Changed
- README에 최종 안정 버전 안내 문구 추가 / Added final stable version notice to README
- CHANGELOG.md 생성 (전체 버전 히스토리) / Created CHANGELOG.md with full version history

---

## [3.10.2]

### Fixed
- ASIO device selection fallback and driver combo sync

## [3.10.1]

### Fixed
- VST bypass state, preset slot oscillation, and cache corruption

## [3.10.0]

### Added
- Multi-instance external control priority (Named Mutex coordination)
- Audio-only mode for secondary instances
- Driver type snapshot/restore (including Output None state)
- Per-direction device loss detection with "(Disconnected)" UI
- Quit button in Settings tab

### Fixed
- Output None bug when switching WASAPI to ASIO
- PluginChainEditor delete failure (name mismatch)
- LevelMeter smoother for visual consistency

## [3.9.12]

### Fixed
- ASIO startup buffer size bounce (wrong device selection)
- pushNotification MPSC race condition
- MonitorOutput alive_ lifetime guard
- AudioRingBuffer reset ordering
- StateBroadcaster slotNames hash

## [3.9.11]

### Fixed
- Output device switching fallback false positive
- Input channel loss when changing WASAPI output device

## [3.9.10]

### Fixed
- ASIO buffer size persistence (desiredBufferSize_ preserves user request)

## [3.9.9]

### Added
- Slot naming (right-click Rename, display as `A|Name`)
- Individual slot export/import (.dppreset)
- StateBroadcaster slot_names array

## [3.9.8]

### Added
- Device fallback protection (intentionalChange_ flag)
- Hotkey drag-and-drop reorder
- MIDI HTTP API test endpoints
- MIDI Learn / key recording cancel buttons

## [3.9.7]

### Added
- Instant preset switching (keep-old-until-ready)
- Settings scope separation (Save/Load vs Full Backup)
- Full Backup/Restore (.dpfullbackup)

## [3.9.6]

### Added
- Device auto-reconnection (dual mechanism: ChangeListener + timer polling)
- Monitor device reconnection
- Device combo click-to-refresh
- StateBroadcaster device_lost/monitor_lost

## [3.9.5]

### Added
- WASAPI Exclusive Mode (5th driver type)
- Audio optimizations (timeBeginPeriod, ScopedNoDenormals, RMS decimation)
- XRun monitoring (rolling 60s window)

## [3.9.4]

### Fixed
- Modal dialog fixes
- HTTP API input gain range
- Constructor SafePointer patterns

## [3.9.3]

### Fixed
- 25 bug fixes: thread safety, lifetime guards, server fixes, RT-safety

## [3.9.2]

### Added
- Categorized logging ([AUDIO], [VST], [PRESET], etc.)
- LogPanel batch flush optimization

### Fixed
- VSTChain lock-ordering hazard with DirectPipeLogger

## [3.9.0]

### Added
- Buffer display in status bar
- Sample rate propagation (main SR applies globally)
- Monitor SR mismatch detection
- Receiver VST SR warning

## [3.8.0]

### Added
- Auto-updater UI (download + auto-restart on Windows)
- CJK font fix (platform-specific font selection)

## [3.7.0]

### Fixed
- Plugin scanner crash (moreThanOneInstanceAllowed)

## [3.6.0]

### Added
- IPC Toggle action (Ctrl+Shift+I)
- Receiver VST buffer size configuration (5 presets)
- Panic mute lockout (all controls locked during panic)

## [3.5.0]

### Added
- NotificationBar (non-intrusive, color-coded, auto-fade)
- LogPanel (Settings tab with log viewer + maintenance tools)

### Fixed
- 4 callAsync lifetime guard fixes

## [3.4.0]

### Added
- System tray tooltip (current state on hover)
- Plugin scanner search/sort
- Audio recording (WAV, lock-free)
- Settings save/load (.dpbackup)
- MIDI plugin parameter mapping
- In-app auto-updater (version check)
