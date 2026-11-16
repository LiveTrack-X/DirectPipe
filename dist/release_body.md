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
Preparation evidence is local. macOS/Linux and final publication CI remain pending.

### VirusTotal

The final distributable scan results, exact SHA-256 identities, analysis dates,
engine coverage/failures and direct report links are included in the attached
`virustotal-report.md` and `virustotal-report.json`. Verify file hashes against
`checksums.sha256`. Local preparation scans do not certify different CI bytes.

최종 배포 파일의 검사 결과·해시·검사 시각·엔진별 검사 범위와 보고서 링크를
첨부한 VirusTotal 보고서에서 확인하세요. 무탐지는 안전 보증이 아닙니다.

### Assets / 배포 파일

Prepared/expected names: `DirectPipe-v4.5.2-Windows.zip`,
`com.directpipe.directpipe.streamDeckPlugin`, `checksums.sha256`,
`virustotal-report.md` and `virustotal-report.json`.
macOS `DirectPipe-v4.5.2-macOS.dmg` and Linux `DirectPipe-v4.5.2-Linux.tar.gz`
require their platform builds; their presence is not claimed by this draft.

[Changes since v4.5.1](https://github.com/LiveTrack-X/DirectPipe/compare/v4.5.1...v4.5.2).
