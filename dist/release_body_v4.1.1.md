## DirectPipe v4.1.1

v4.1.1 is a host-side hotfix after v4.1.0. It fixes the preset-slot cache crash reported in issue #4, tightens settings backup/restore behavior, and includes audio-device restore hardening for channel fallback, ASIO sample-rate preservation, startup target waiting, and zero-active-channel recovery.

This release does not change the control API, preset schema, Stream Deck action schema, or Receiver IPC protocol.

v4.1.0 이후 호스트 쪽 안정화 핫픽스입니다. issue #4 프리셋 슬롯 캐시 크래시를 수정하고, 설정/전체 백업 복원 정확도와 시작 시 저장 장치 복원 대기, 채널 fallback, ASIO 샘플레이트 보존, zero-active-channel 복구를 강화했습니다.

---

### Highlights / 주요 변경

#### 1) Preset-slot cache crash fix / 프리셋 슬롯 캐시 크래시 수정
- Fixed a cache-hit preset switch crash when a slot contains duplicate VST plugins with the same name/path.
- Cache-hit state refresh now consumes the fresh slot-file state by plugin index, so duplicate entries no longer steal the first matching plugin's moved-out state block.
- Cache reuse now validates plugin descriptor identity (`uniqueId`, `fileOrIdentifier`, and format) when descriptor data is available.
- If a cached graph swap cannot be completed, DirectPipe keeps the old chain alive, discards the bad cache, and falls back to the normal async load path.
- Fresh slot-file state is copied into the cached-swap request instead of moved out before swap success, so the normal async fallback keeps saved plugin state intact.

#### 2) Save/restore correctness / 저장 및 복원 정확도
- Settings-only `.dpbackup` restore no longer imports plugin chain state or changes `activeSlot`, preventing the next autosave from accidentally overwriting slot A or another quick slot.
- Full backup export saves the active quick-slot file before collecting slot files, so the full backup matches the current chain state.
- Full backup export now uses the same `.bak` fallback recovery as normal preset loading, so a recoverable slot is included even if the main slot file is corrupt.
- Full restore is now exact: slots missing from the backup are removed from disk instead of leaving stale local slots behind.
- Full restore clears stale `.bak`, `.backup`, `.tmp`, and legacy numeric slot families after successful slot restore and refreshes slot occupancy, slot names, and preload cache state.
- Settings/full-backup import now reports audio-settings import failure instead of silently returning success after a partial restore.

#### 3) Audio device restore hardening / 오디오 장치 복원 강화
- Saved channel masks now fall back to driver-default channel layouts when an explicit mask is rejected, rather than forcing channel 0 and accidentally downgrading stereo devices to mono.
- Explicit saved channel masks must intersect the runtime active channel mask before a device is considered ready.
- Startup, restore, driver switch, and reconnection paths detect devices that open with zero active input/output channels and retry with driver-default channels.
- Startup restore pending now clears only after the saved device targets are actually active, so a fallback device does not masquerade as a completed restore.
- ASIO restore accepts the actual device buffer size, but preserves an explicit requested sample rate when the driver reports a mismatched value.
- XRun diagnostics now include throttled device/SR/BS and active-channel snapshots to make restore and routing failures easier to trace.

#### 4) Monitor output robustness / 모니터 출력 안정성
- Monitor output initialization now retries with driver-default output channels and, as a last resort, driver buffer defaults.
- Monitor devices that open with zero active output channels are treated as not ready and schedule recovery instead of reporting Active.

#### 5) Slot transition diagnostics / 슬롯 전환 진단
- Active slot transitions are now logged consistently across save, load, async load, cache hit, import, delete, clear, and GUI slot clicks.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, settings backups, and full backups remain compatible.
- **No Receiver VST replacement required for these host-side fixes**: existing Receiver VST installs do not need manual replacement to benefit from the preset-cache, backup/restore, and device-restore fixes. The bundled Receiver plugin remains version-aligned for new installs.
- **Full restore behavior is stricter / 전체 복원 동작 강화**: `.dpfullbackup` restore now removes local quick slots that are not present in the backup.
- **Release assets are built by CI / 릴리즈 산출물은 CI에서 빌드**: this GitHub release is created with notes only; GitHub Actions builds and uploads the Windows, macOS, Linux, Stream Deck, and checksum assets.

---

### Validation / 검증
- Local Windows Release verification built: `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, and `directpipe-host-tests`.
- `directpipe-tests`: 52 passed.
- `directpipe-host-tests`: 310 tests; 308 passed, 2 environment-dependent tests skipped.
- `ctest`: 362 registered; 0 failed, 2 skipped.
- Stream Deck `manifest.json`, `package.json`, and `package-lock.json` parsed successfully at version `4.1.1` / `4.1.1.0`.
- Text integrity and `tools/pre-release-test.sh --version-only` passed; `git diff --check` passed with only expected CRLF conversion warnings.
- Final release assets are rebuilt by GitHub Actions after the release is created.

---

### Downloads / 다운로드
- `DirectPipe-v4.1.1-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.1.1-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.1.1-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck control package, CI-built.
- `checksums.sha256` — generated by CI for all uploaded assets.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.0...v4.1.1
