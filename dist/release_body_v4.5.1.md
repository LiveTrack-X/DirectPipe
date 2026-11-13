## DirectPipe v4.5.1

2026-11-13

Built-in noise removal now works at supported sample rates other than 48 kHz,
and both input/output meters show L/R separately. Processing status and reported
latency now match the implementation.

내장 노이즈 제거를 다른 샘플레이트에서도 사용할 수 있게 하고, 입출력 미터에
L/R을 각각 표시합니다. 실제 처리 상태와 누락됐던 내부 지연 보고도 수정합니다.

- Windows-only **Install DPWIM...** appears directly below Built-in. It checks
  the official stable release and installs/updates a verified external VST3
  copy. DPWIM remains separate from built-ins and Auto; active instances and
  saved plugin paths are preserved.
- Input L/R bars now show selected capture channels before Mono mixing or
  duplication, with gain/mute applied. A silent input no longer shows the
  other channel's copied activity. Mono audio gain and scalar API stay unchanged.

### Upgrade / 업데이트

Preserve your configuration and close DirectPipe yourself before replacing
files. Use Settings > Update Receiver... to review existing Receiver versions
after updating the host. The updater in an older installed version performs
the first upgrade; keep recovery files if it fails.

기존 설정을 보존하고 파일 교체 전 DirectPipe를 직접 종료하세요. 본체 갱신 후
Settings > Update Receiver...에서 Receiver 버전도 확인하세요. 첫 업데이트는
기존 설치본의 업데이터가 수행하며, 실패 시 복구 파일을 보존하세요.

### Changes / 변경 사항

- Internal low-pass conversion supports finite 8-192 kHz RNNoise operation;
  native 48 kHz retains the existing processing path.
- Chain/editor status follows native, converted and inactive processing.
  Obsolete non-48 kHz warnings from Auto and external controls are removed.
- Input/output meters have independent L/R bars and peak holds.
- Reported latency includes RNNoise's intrinsic two-frame delay. Native
  processing reports 30 ms rather than 10 ms; this corrects reporting without
  inserting a new delay into the 48 kHz audio path.
- Output FIFO indexing stays ordered across the long-session counter boundary.
- 내장 노이즈 제거 내부 변환·동작 상태·입출력 L/R 미터를 개선하고,
  지연 보고 및 장시간 FIFO 카운터 경계를 바로잡습니다.

### Compatibility and verification / 호환성·검증

Preset/settings formats, existing Mono gain, control API and Stream Deck action
IDs are preserved. Host and Receiver sample rates must still match. Multi-mic,
ASMR routing and independent USB aggregation are not included.

설정·프리셋·기존 Mono 게인·제어 API는 유지합니다. 본체와 Receiver는 계속 같은
샘플레이트를 사용해야 합니다. 멀티마이크·ASMR 라우팅은 이번 범위에 포함하지 않습니다.

Verification details and hardware/listening limits:
[4.5.1 readiness](https://github.com/LiveTrack-X/DirectPipe/blob/main/docs/RELEASE_4_5_1.md).
Software checks do not establish physical listening or long-session hardware acceptance.

### Downloads and checksums / 다운로드·체크섬

Download the platform asset below and verify it against `checksums.sha256`.
Windows: `DirectPipe-v4.5.1-Windows.zip`; macOS: `DirectPipe-v4.5.1-macOS.dmg`;
Linux: `DirectPipe-v4.5.1-Linux.tar.gz`; Stream Deck: `com.directpipe.directpipe.streamDeckPlugin`.
DPWIM itself is downloaded separately only when the Windows install action is clicked.

아래 플랫폼별 파일과 `checksums.sha256`을 확인하세요. DPWIM은 본체 ZIP에
포함되지 않으며 Windows의 설치 메뉴를 클릭할 때 별도로 다운로드합니다.

### Comparison / 비교

[Changes since v4.5.0](https://github.com/LiveTrack-X/DirectPipe/compare/v4.5.0...v4.5.1).
