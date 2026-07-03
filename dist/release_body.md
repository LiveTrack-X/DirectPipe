## DirectPipe v4.1.1

v4.1.1 is a host-side hotfix after v4.1.0. It fixes the preset-slot cache crash reported in issue #4, tightens settings backup/restore behavior, and includes audio-device restore hardening for channel fallback, ASIO sample-rate preservation, startup target waiting, and zero-active-channel recovery.

v4.1.1은 v4.1.0 이후의 호스트 쪽 핫픽스입니다. issue #4에서 보고된 프리셋 슬롯 캐시 크래시를 수정하고, 설정 백업/복원 동작을 더 정확하게 만들었으며, 채널 fallback, ASIO 샘플레이트 보존, 시작 시 저장 장치 대기, zero-active-channel 복구를 포함한 오디오 장치 복원 안정성을 강화했습니다.

This release does not change the control API, preset schema, Stream Deck action schema, or Receiver IPC protocol.

이번 릴리즈는 control API, preset schema, Stream Deck action schema, Receiver IPC protocol을 변경하지 않습니다.

---

### Highlights / 주요 변경

#### 1) Preset-slot cache crash fix / 프리셋 슬롯 캐시 크래시 수정
- Fixed a cache-hit preset switch crash when a slot contains duplicate VST plugins with the same name/path.
- 동일한 이름/경로의 VST 플러그인이 한 슬롯에 중복 포함된 경우 cache-hit 프리셋 전환 중 발생하던 크래시를 수정했습니다.
- Cache-hit state refresh now consumes the fresh slot-file state by plugin index, so duplicate entries no longer steal the first matching plugin's moved-out state block.
- Cache-hit 상태 갱신은 이제 새 슬롯 파일 상태를 플러그인 인덱스 기준으로 사용하므로, 중복 항목이 첫 번째 매칭 플러그인의 이동된 상태 블록을 잘못 가져가지 않습니다.
- Cache reuse now validates plugin descriptor identity (`uniqueId`, `fileOrIdentifier`, and format) when descriptor data is available.
- 캐시 재사용 시 descriptor 데이터가 있으면 플러그인 식별 정보(`uniqueId`, `fileOrIdentifier`, format)를 검증합니다.
- If a cached graph swap cannot be completed, DirectPipe keeps the old chain alive, discards the bad cache, and falls back to the normal async load path.
- 캐시된 그래프 교체를 완료할 수 없으면 DirectPipe는 기존 체인을 유지하고 잘못된 캐시를 버린 뒤 일반 async load 경로로 fallback합니다.
- Fresh slot-file state is copied into the cached-swap request instead of moved out before swap success, so the normal async fallback keeps saved plugin state intact.
- 새 슬롯 파일 상태는 swap 성공 전에 이동되지 않고 cached-swap 요청으로 복사되므로, 일반 async fallback에서도 저장된 플러그인 상태가 보존됩니다.

#### 2) Save/restore correctness / 저장 및 복원 정확도
- Settings-only `.dpbackup` restore no longer imports plugin chain state or changes `activeSlot`, preventing the next autosave from accidentally overwriting slot A or another quick slot.
- Settings-only `.dpbackup` 복원은 더 이상 플러그인 체인 상태를 가져오거나 `activeSlot`을 변경하지 않으므로, 다음 autosave가 slot A 또는 다른 quick slot을 의도치 않게 덮어쓰는 일을 방지합니다.
- Full backup export saves the active quick-slot file before collecting slot files, so the full backup matches the current chain state.
- Full backup export는 슬롯 파일을 수집하기 전에 활성 quick-slot 파일을 저장하므로, 전체 백업이 현재 체인 상태와 일치합니다.
- Full backup export now uses the same `.bak` fallback recovery as normal preset loading, so a recoverable slot is included even if the main slot file is corrupt.
- Full backup export는 일반 프리셋 로딩과 같은 `.bak` fallback 복구를 사용하므로, 메인 슬롯 파일이 손상되어도 복구 가능한 슬롯은 백업에 포함됩니다.
- Full restore is now exact: slots missing from the backup are removed from disk instead of leaving stale local slots behind.
- Full restore는 이제 정확 복원 방식입니다. 백업에 없는 슬롯은 오래된 로컬 슬롯으로 남지 않고 디스크에서 제거됩니다.
- Full restore clears stale `.bak`, `.backup`, `.tmp`, and legacy numeric slot families after successful slot restore and refreshes slot occupancy, slot names, and preload cache state.
- Full restore는 슬롯 복원이 성공하면 오래된 `.bak`, `.backup`, `.tmp`, legacy numeric slot 계열을 정리하고 slot occupancy, slot name, preload cache 상태를 갱신합니다.
- Settings/full-backup import now reports audio-settings import failure instead of silently returning success after a partial restore.
- Settings/full-backup import는 부분 복원 후 조용히 성공을 반환하지 않고 audio-settings import 실패를 보고합니다.

#### 3) Audio device restore hardening / 오디오 장치 복원 강화
- Saved channel masks now fall back to driver-default channel layouts when an explicit mask is rejected, rather than forcing channel 0 and accidentally downgrading stereo devices to mono.
- 저장된 channel mask가 거부되면 channel 0을 강제로 사용해 stereo 장치를 mono로 낮추는 대신 driver-default channel layout으로 fallback합니다.
- Explicit saved channel masks must intersect the runtime active channel mask before a device is considered ready.
- 명시적으로 저장된 channel mask는 runtime active channel mask와 교집합이 있어야 장치가 ready 상태로 인정됩니다.
- Startup, restore, driver switch, and reconnection paths detect devices that open with zero active input/output channels and retry with driver-default channels.
- 시작, 복원, 드라이버 전환, 재연결 경로는 active input/output channel이 0개로 열린 장치를 감지하고 driver-default channel로 다시 시도합니다.
- Startup restore pending now clears only after the saved device targets are actually active, so a fallback device does not masquerade as a completed restore.
- Startup restore pending은 저장된 장치 target이 실제로 활성화된 뒤에만 해제되므로, fallback 장치가 복원 완료로 잘못 처리되지 않습니다.
- ASIO restore accepts the actual device buffer size, but preserves an explicit requested sample rate when the driver reports a mismatched value.
- ASIO restore는 실제 장치 buffer size는 수용하지만, 드라이버가 다른 값을 보고해도 명시적으로 요청된 sample rate는 보존합니다.
- XRun diagnostics now include throttled device/SR/BS and active-channel snapshots to make restore and routing failures easier to trace.
- XRun diagnostics에는 throttled device/SR/BS 및 active-channel snapshot이 포함되어 복원 및 라우팅 실패를 더 쉽게 추적할 수 있습니다.

#### 4) Monitor output robustness / 모니터 출력 안정성
- Monitor output initialization now retries with driver-default output channels and, as a last resort, driver buffer defaults.
- Monitor output 초기화는 driver-default output channel로 재시도하며, 마지막 fallback으로 driver buffer default도 사용합니다.
- Monitor devices that open with zero active output channels are treated as not ready and schedule recovery instead of reporting Active.
- active output channel이 0개로 열린 monitor 장치는 Active로 보고되지 않고 not ready로 처리되어 복구를 예약합니다.

#### 5) Slot transition diagnostics / 슬롯 전환 진단
- Active slot transitions are now logged consistently across save, load, async load, cache hit, import, delete, clear, and GUI slot clicks.
- Active slot 전환은 save, load, async load, cache hit, import, delete, clear, GUI slot click 경로 전반에서 일관되게 로그로 남습니다.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, settings backups, and full backups remain compatible.
- **No API/state model break / API 및 상태 모델 호환 유지**: Stream Deck, HTTP, WebSocket, presets, settings backups, full backups는 계속 호환됩니다.
- **No Receiver VST replacement required for these host-side fixes**: existing Receiver VST installs do not need manual replacement to benefit from the preset-cache, backup/restore, and device-restore fixes. The bundled Receiver plugin remains version-aligned for new installs.
- **No Receiver VST replacement required for these host-side fixes / 호스트 쪽 수정에 Receiver VST 교체 불필요**: 기존 Receiver VST 설치본은 preset-cache, backup/restore, device-restore 수정 효과를 얻기 위해 수동 교체할 필요가 없습니다. 새 설치용 번들 Receiver plugin은 버전에 맞춰 유지됩니다.
- **Full restore behavior is stricter / 전체 복원 동작 강화**: `.dpfullbackup` restore now removes local quick slots that are not present in the backup.
- **Full restore behavior is stricter / 전체 복원 동작 강화**: `.dpfullbackup` 복원은 백업에 없는 로컬 quick slot을 제거합니다.
- **Release assets are built by CI / 릴리즈 산출물은 CI에서 빌드**: this GitHub release is created with notes only; GitHub Actions builds and uploads the Windows, macOS, Linux, Stream Deck, and checksum assets.
- **Release assets are built by CI / 릴리즈 산출물은 CI에서 빌드**: 이 GitHub release는 notes-only로 생성되며, GitHub Actions가 Windows, macOS, Linux, Stream Deck, checksum 산출물을 빌드하고 업로드합니다.

---

### Validation / 검증
- Local Windows Release verification built: `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, and `directpipe-host-tests`.
- Local Windows Release 검증에서 `DirectPipe`, `DirectPipeReceiver_VST3`, `DirectPipeReceiver_VST`, `directpipe-tests`, `directpipe-host-tests` 빌드를 확인했습니다.
- `directpipe-tests`: 52 passed.
- `directpipe-tests`: 52개 통과.
- `directpipe-host-tests`: 310 tests; 308 passed, 2 environment-dependent tests skipped.
- `directpipe-host-tests`: 총 310개 중 308개 통과, 환경 의존 테스트 2개 skipped.
- `ctest`: 362 registered; 0 failed, 2 skipped.
- `ctest`: 등록 테스트 362개, 실패 0개, skipped 2개.
- Stream Deck `manifest.json`, `package.json`, and `package-lock.json` parsed successfully at version `4.1.1` / `4.1.1.0`.
- Stream Deck `manifest.json`, `package.json`, `package-lock.json`은 version `4.1.1` / `4.1.1.0` 기준으로 정상 parsing됐습니다.
- Text integrity, JSON metadata validation, Stream Deck package manifest validation, version metadata validation, and `git diff --check` passed.
- Text integrity, JSON metadata validation, Stream Deck package manifest validation, version metadata validation, `git diff --check`가 통과했습니다.
- Final release assets are rebuilt by GitHub Actions after the release is created.
- 최종 릴리즈 산출물은 release 생성 후 GitHub Actions에서 다시 빌드됩니다.

---

### Downloads / 다운로드
- `DirectPipe-v4.1.1-Windows.zip` — Windows stable artifact, CI-built.
- `DirectPipe-v4.1.1-Windows.zip` — Windows 안정 버전 산출물이며 CI에서 빌드했습니다.
- `DirectPipe-v4.1.1-macOS.dmg` — macOS beta artifact, CI-built.
- `DirectPipe-v4.1.1-macOS.dmg` — macOS beta 산출물이며 CI에서 빌드했습니다.
- `DirectPipe-v4.1.1-Linux.tar.gz` — Linux experimental artifact, CI-built.
- `DirectPipe-v4.1.1-Linux.tar.gz` — Linux experimental 산출물이며 CI에서 빌드했습니다.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck control package, CI-built.
- `com.directpipe.directpipe.streamDeckPlugin` — Stream Deck 제어 패키지이며 CI에서 빌드했습니다.
- `checksums.sha256` — generated by CI for all uploaded assets.
- `checksums.sha256` — 업로드된 모든 산출물에 대해 CI에서 생성한 checksum 파일입니다.

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.0...v4.1.1

**전체 변경 비교**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.1.0...v4.1.1
