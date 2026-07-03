# DirectPipe Release Notes

> This is a user-facing release summary. For detailed developer change history, see [CHANGELOG.md](../CHANGELOG.md).

## DirectPipe v4.1.1

v4.1.1 is a host-side hotfix after v4.1.0. It fixes the issue #4 preset-slot cache crash with duplicate VST plugins, tightens settings backup/full restore behavior, and includes audio-device restore hardening for channel fallback, ASIO sample-rate preservation, and zero-active-channel recovery.

v4.1.0 이후 호스트 쪽 안정화 핫픽스입니다. issue #4 프리셋 슬롯 캐시 크래시를 수정하고, 설정/전체 백업 복원 정확도와 시작 시 저장 장치 복원 대기, 채널 fallback, ASIO 샘플레이트 보존, zero-active-channel 복구를 강화했습니다.

### Highlights / 주요 변경
- Duplicate-plugin preset cache hits now refresh state by plugin index and reject descriptor mismatches.
- Failed cached graph swaps keep the old chain alive, preserve fresh saved plugin state for fallback, and fall back to normal async loading.
- `.dpbackup` restore no longer changes `activeSlot` or imports chain state.
- `.dpfullbackup` restore is exact: missing slots are removed, restored slots clear `.bak`/`.backup`/`.tmp` and legacy numeric slot families, failed audio-settings imports are reported, and slot/preload caches are refreshed.
- Device restore/reconnect paths retry driver-default channels when explicit masks fail, only clear startup restore pending after saved targets are active, and monitor output no longer reports Active with zero active output channels.

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, settings backups, and full backups remain compatible.
- **No Receiver VST replacement required for these host-side fixes**: existing Receiver VST installs do not need manual replacement for these host-side fixes.
- **Full restore behavior is stricter / 전체 복원 동작 강화**: full restore now removes local quick slots that are absent from the backup.

### Validation / 검증
- Windows Release build passed for `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, and `directpipe-host-tests`.
- `directpipe-tests`: 52 passed.
- `directpipe-host-tests`: 310 tests; 308 passed, 2 environment-dependent tests skipped.
- `ctest`: 362 registered; 0 failed, 2 skipped.
- Stream Deck JSON metadata parsed at version `4.1.1` / `4.1.1.0`; text integrity, version-only, and `git diff --check` passed with expected CRLF warnings.

---

## DirectPipe v4.1.0

v4.1.0 is the next public release after v4.0.9. It includes the startup/tray device-restore and Audit Mode work prepared after v4.0.9, plus the adaptive PLL monitor-output bridge.

This release targets the real crackle/cutout and OUT XRun increase seen when the main OUT path and a separate monitor device run on independent WASAPI hardware clocks. Monitor output still receives the already processed post-VST/post-Safety buffer; it does not run the VST chain a second time.

v4.0.9 이후 준비했던 시작/트레이 장치 복원, Audit Mode 확장, adaptive PLL 기반 모니터 출력 브리지를 포함한 릴리즈입니다. 별도 모니터 장치 사용 시 발생하던 실제 끊김/지지직 및 OUT XRun 증가 문제를 줄이는 데 초점을 맞췄습니다.

---

### Highlights / 주요 변경

#### 1) Adaptive PLL monitor bridge / 적응형 PLL 모니터 브리지
- Monitor output now uses fractional read playback with linear interpolation on the monitor consumer side.
- Normal independent-device clock drift is absorbed by tiny playback-ratio corrections instead of frame discard.
- Runtime producer/consumer block-size changes re-prime the monitor bridge so the new target fill is honored before playback resumes.
- Partial monitor underruns re-prime before playback resumes, and repeated underruns gradually raise the target fill.
- Near-overflow emergency trim remains a safety fallback only and gets a short fade-in.
- This is not double processing: the VST chain still runs once in the main audio path.

#### 2) Startup/tray WASAPI restore / 시작 및 트레이 WASAPI 복원
- Settings restore now remembers saved input/output device targets even when Windows has not finished enumerating devices at login or tray startup.
- If the saved mic or output device is not available yet, DirectPipe keeps retrying for that explicit target instead of accepting an unintended fallback device as a successful restore.
- Output auto-mute remains active while the saved output target is missing, reducing wrong-device output risk.

#### 3) Main OUT XRun protection / 메인 OUT XRun 보호
- The main RT callback only pushes monitor audio when the separate monitor output is actually active.
- Monitor RMS level is cleared when monitor routing is disabled or inactive, keeping Audit Mode and UI meters from showing stale monitor audio.
- On Windows, the monitor callback remains below the main OUT callback priority by using MMCSS `Pro Audio` normal priority.
- The 60-second XRun rolling window now advances by every elapsed second after UI/device stalls, so stale buckets age out on time.
- Audit Mode `xrunDelta` now comes from a monotonic XRun event counter instead of comparing rolling-window totals.

#### 4) Audit diagnostics / Audit 진단 확장
- Audit Mode now emits rate-limited `AUDIO` and `MONITOR` snapshots from the message thread.
- `AUDIO` snapshots include desired/actual devices, SR/BS, CPU/proc time, XRun and callback-overrun deltas, mute/lost/IPC/limiter flags.
- `MONITOR` snapshots include fill/target/PLL state, block sizes, dropped/underrun/trim deltas, sample rate, ring capacity, and priming state.
- RT callbacks only update atomics/counters; logging stays on the message thread.

#### 5) Platform support / OS별 지원
- **Windows 10/11 x64**: stable release target. v4.1.0 local pre-release verification completed.
- **macOS 10.15+ universal**: beta. Source/CMake/CI release path exists; real-hardware audio validation is limited.
- **Linux x86_64**: experimental. Source/CMake/CI release path exists; behavior can vary by distribution, desktop environment, and audio server.
- **Stream Deck plugin**: separate cross-platform package targeting Windows 10+, macOS 10.15+, and Stream Deck 6.9+.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, and backups remain compatible.
- **No Receiver VST replacement required for these host-side fixes**: existing Receiver VST installs do not need manual replacement to benefit from the startup, monitor-output, and diagnostics fixes. The bundled Receiver plugin remains version-aligned for new installs.
- **Audit Mode / Audit 모드**: enable it only while reproducing monitor or XRun issues, then compare monitor-off vs monitor-on runs.
- **Release assets are built by CI / 릴리즈 산출물은 CI에서 빌드**: this GitHub release is created with notes only; GitHub Actions builds and uploads the Windows, macOS, Linux, Stream Deck, and checksum assets.

---

### Validation / 검증
- Local Windows Release verification built: `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, and `directpipe-host-tests`.
- `directpipe-tests`: 52 passed.
- `directpipe-host-tests`: 291 passed, 2 environment-dependent tests skipped.
- Text integrity, JSON metadata validation, Stream Deck package manifest validation, and `git diff --check` passed.
- Final release assets are rebuilt by GitHub Actions after the release is created.

---

### Downloads / 다운로드
- `DirectPipe-v4.1.0-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.1.0-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.1.0-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck control package, CI-built.
- `checksums.sha256` — generated by CI for all uploaded assets.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.9...v4.1.0

For v4.0.9 and earlier history, see [CHANGELOG.md](../CHANGELOG.md).
