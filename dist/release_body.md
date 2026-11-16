## DirectPipe v4.5.2

2026-11-16

### Changes / 변경 사항

- Add manual Built-in **NoiseRemover (DSP)**: two-second Learn with a frozen
  noise profile, or adaptive Realtime estimation. Excluded from AUTO.
- Show live input (including voice), estimated noise and actual per-frequency
  attenuation inside the DSP editor, with L/R selection.
- Report actual DSP delay in samples/ms and chain compensation: 256 samples
  (5.333 ms) at 48 kHz, including learning/no profile/zero reduction. Graph adds
  no audio delay. Mode/reduction/completed manual profile persist in presets.
- New Receiver instances default to **Medium (1024 frames)**; saved choices remain.
- 내장 DSP 노이즈 제거의 Learn/Realtime과 입력·학습 노이즈·감쇠 그래프를 추가합니다.
  AUTO에는 포함하지 않으며 Receiver 신규 기본 버퍼는 Medium입니다.

### Using Learn / Learn 사용

Add Plugin > Built-in > NoiseRemover (DSP), select Learn, and capture two
seconds with only background noise present. The orange noise estimate freezes
when capture finishes; blue shows current input including voice, and purple
shows actual applied reduction. Relearn when the noise changes.

배경 잡음만 있는 상태에서 Learn을 누르세요. 2초 학습 후 주황색 노이즈
프로필이 고정됩니다. 파랑은 음성을 포함한 현재 입력, 보라는 실제 감쇠량입니다.

Realtime uses statistical DSP, not AI speech separation. Weak sustained voice
may be attenuated; these software checks do not establish vocal/listening or
physical loopback acceptance. Driver/Receiver buffers add separate latency.

### Upgrade / 업데이트

Preserve settings and close DirectPipe yourself before replacing files. Review
Receiver versions through Settings > Update Receiver... after updating the host.
Existing Receiver projects retain their saved buffer choice. RNNoise and AUTO
retain their existing behavior. DPWIM remains a separate optional external VST.

### Verification / 검증

[4.5.2 readiness and limits](https://github.com/LiveTrack-X/DirectPipe/blob/main/docs/RELEASE_4_5_2.md).
[Full validated CI](https://github.com/LiveTrack-X/DirectPipe/actions/runs/43801035670):
Windows 825 passed/4 skipped; macOS 710 passed/40 skipped; Linux 710 passed/40 skipped;
Stream Deck 28 passed. All have 0 failures. The exact Windows candidate API and
native core suites passed. Public source tag `v4.5.2` matches the tested source tree.

Windows binaries are unsigned. macOS bundles use ad-hoc signing and are not
notarized. Hardware/listening and prolonged live-session acceptance remain unclaimed.

### VirusTotal

Final distributable bytes checked on 2026-11-16 (KST). Post-scan SHA-256 values
match the files staged for this release.

| File | Detections / checked engines | Report |
|---|---:|---|
| Windows ZIP | **0/66** | [VirusTotal](https://www.virustotal.com/gui/file/428732cdbe78b03c1f853c25ddaf5537ba553a726edd9115029a933c8ea14134) |
| Windows host | **0/71** | [VirusTotal](https://www.virustotal.com/gui/file/ed053918fe6509d62f911425070564e17a9d3cc3d3254375cd99341b94af4036) |
| Receiver VST2 | **0/71** | [VirusTotal](https://www.virustotal.com/gui/file/f2876b7c371e008b56776805fd0fb3485d682498904db9f6d45979cad903ed91) |
| Receiver VST3 | **0/71** | [VirusTotal](https://www.virustotal.com/gui/file/071dbbb848d55e78e94b892fcff053262df02ac31b6d04cd4e9334bb76cadc09) |
| macOS DMG | **0/61** | [VirusTotal](https://www.virustotal.com/gui/file/ebcbc09ba59bad1a2d58775b32e47044a1bcdf1f4eda0f99793f5e02d9f3b63c) |
| Linux tar.gz | **0/62** | [VirusTotal](https://www.virustotal.com/gui/file/feb07908b1b4f5c866faa6c41faa37ad04546ca579421f1a61d42476970ac505) |
| Stream Deck | **0/64** | [VirusTotal](https://www.virustotal.com/gui/file/8d2f0d3c41f003a21c56a94c0e9bca993a971eacec88bfca77110a7d98a09f1c) |

검사 불가·실패·시간 초과 엔진은 위 분모에서 제외합니다. Windows ZIP과 Stream
Deck에 각각 엔진 실패 1건이 있습니다. 전체 범위·시각·정확한 해시는 첨부한
`virustotal-report.md` / `.json`에 기록했습니다. 무탐지는 안전 보증이 아닙니다.

Unsupported/failed/timeout engines are recorded separately. Archive scans do not
prove exhaustive coverage of every nested file; the three Windows PE payloads
also have individual reports. Rebuilt or differently signed bytes need new reports.

### Assets / 배포 파일

- `DirectPipe-v4.5.2-Windows.zip`: DirectPipe.exe + Receiver VST2/VST3.
- `DirectPipe-v4.5.2-macOS.dmg`: universal arm64+x86_64 app + Receiver VST2/VST3/AU.
- `DirectPipe-v4.5.2-Linux.tar.gz`: x86_64 app + Receiver VST2/VST3.
- `com.directpipe.directpipe.streamDeckPlugin`: version 4.5.2.0.
- `checksums.sha256`, `virustotal-report.md`, `virustotal-report.json`.

Verify the downloaded payload/report files against `checksums.sha256`.
DPWIM remains a separate optional download through the Windows install action.

[Changes since v4.5.1](https://github.com/LiveTrack-X/DirectPipe/compare/v4.5.1...v4.5.2).
