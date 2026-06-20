## DirectPipe v4.0.9

v4.0.8 이후, 모니터 지연 누적 보정이 일부 장치 조합에서 너무 공격적으로 동작해 모니터 버퍼를 크게 잡아야 끊김이 줄어드는 문제를 보정한 핫픽스 릴리스입니다.
This v4.0.9 hotfix tunes the monitor drift compensation added in v4.0.8 so smaller monitor buffers do not get over-trimmed when the main audio callback uses a larger block size.
It also hardens preset slot switching and Factory Reset cleanup so stale preload entries or backup files do not keep old slot state alive.
The same audit also adds defensive empty/short-buffer guards in the host audio paths without requiring users to replace an already installed Receiver VST plugin.

---

### Highlights / 주요 변경

#### 1) Monitor drift trim tuning / 모니터 드리프트 trim 보정
- 모니터 출력은 여전히 VST/Safety 처리 이후의 메인 오디오 버퍼를 그대로 받아 출력하며, 별도의 VST 이중 처리는 하지 않습니다.
- Monitor output still receives the already processed post-VST/post-Safety audio buffer; it does not run a second VST processing pass.
- 4.0.8의 오래된 monitor ring-buffer 프레임 정리는 유지하되, trim 목표치를 메인 오디오 콜백 producer block과 모니터 장치 consumer block을 함께 고려하도록 조정했습니다.
- The stale-frame trim added in v4.0.8 is kept, but its target fill now accounts for both the main audio callback producer block and the monitor-device consumer block.
- 메인 버퍼가 512 samples이고 모니터 버퍼가 128 samples처럼 더 작은 경우에도 ring buffer를 메인 콜백 단위보다 얕게 깎지 않아, 짧은 모니터 버퍼에서 발생할 수 있는 끊김을 줄입니다.
- When the main buffer is larger than the monitor buffer, for example 512 samples vs 128 samples, DirectPipe no longer trims the ring buffer below the main callback granularity, reducing monitor dropouts.

#### Host buffer edge-case guards
- `AudioRingBuffer`, `OutputRouter`, `MonitorOutput`, `SharedMemWriter`, and `AudioRecorder` now defensively handle zero-channel, null-channel, or short-source-buffer calls so fallback silence paths do not reuse stale audio or read past the source buffer.

#### Preset slot switch stability
- DirectPipe now validates cached preset-slot plugin instances against the current slot file before using them.
- If a slot was reset, restored, deleted, imported, or otherwise changed behind the cache, DirectPipe discards the stale cache and reloads the slot normally instead of swapping mismatched plugin instances.
- This addresses cases where switching to a specific slot could close the host process.

#### Factory Reset slot cleanup
- Factory Reset and Clear All Presets now clear runtime slot names, occupied-slot state, pending slot loads, and preload cache state immediately after deleting preset files.
- Deleted slot names should no longer remain visible after a reset.

#### Backup-file cleanup
- Preset cleanup now removes `.bak`, `.backup`, `.tmp`, and legacy numeric slot-file variants together.
- Existing preset files remain compatible. If an old slot name was already stuck before upgrading, run Factory Reset or Clear All Presets once on v4.0.9 so the new cleanup path can remove stale backup files.

#### 2) Regression tests / 회귀 테스트
- 작은 모니터 버퍼, 큰 메인 producer block, 0채널/null-channel 입력, 짧은 source buffer 경계를 검증하는 회귀 테스트를 추가했습니다.
- Added `MonitorDriftPolicyTest`, `AudioRingBufferTest`, and `OutputRouterTest` coverage for small-monitor-buffer, large-producer-block, zero-channel, null-channel, and short-source-buffer cases.
- Release host tests were rebuilt and run. Host tests passed with 272 passed, 2 environment-dependent tests skipped.

---

### Upgrade Notes / 업그레이드 안내
- **No API/state model break / API 및 상태 모델 호환 유지**: 이 릴리스는 control API 또는 state schema의 breaking change를 포함하지 않습니다.
- **Monitor output / 모니터 출력**: 가장 낮은 지연을 원하면 기존처럼 같은 오디오 인터페이스의 ASIO 모니터링 또는 하드웨어 다이렉트 모니터링이 가장 안정적입니다. 별도 shared-mode 모니터 장치를 쓰는 경우에는 이번 보정으로 4.0.8보다 작은 모니터 버퍼에서도 안정성이 좋아집니다.
- **Monitor drift / 모니터 지연 누적**: 서로 다른 물리 장치를 입력과 모니터 출력으로 쓰는 경우 clock drift 자체는 발생할 수 있지만, DirectPipe가 오래된 monitor buffer를 정리해 누적 지연을 제한합니다.
- **Windows unblock / Windows 차단 해제**: 다운로드한 실행 파일이 차단되면 `DirectPipe.exe` 우클릭 -> **속성** -> **일반** -> **보안** -> **차단 해제** -> **적용/확인**을 누르세요.

---

### Downloads / 다운로드
- `DirectPipe-v4.0.9-Windows.zip`
- `DirectPipe-v4.0.9-macOS.dmg`
- `DirectPipe-v4.0.9-Linux.tar.gz`
- `com.directpipe.directpipe.streamDeckPlugin`
- `checksums.sha256`

**Full Changelog**: https://github.com/LiveTrack-X/DirectPipe/compare/v4.0.8...v4.0.9
