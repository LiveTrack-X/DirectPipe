## DirectPipe v4.5.2

2026-11-16 (KST)

[English](#english) · [한국어](#한국어)

### English

#### Changes

- Add manual Built-in **NoiseRemover (DSP)** with **Learn** (a frozen profile captured over two seconds) and **Realtime** (adaptive estimation). It is excluded from AUTO.
- Show current input, including voice, the estimated noise profile and actual frequency-by-frequency attenuation inside the DSP editor. Select L / Mono or R independently for display.
- Report actual DSP latency in samples and milliseconds, including chain compensation: **256 samples / 5.333 ms at 48 kHz**. The same delay remains during learning, without a profile and at zero reduction. The graph adds no audio delay. Mode, reduction and the completed manual profile are saved in presets.
- New Receiver instances default to **Medium (1024 frames)**. Existing projects retain their saved buffer choices.

#### Learn and Realtime usage

1. Open **Add Plugin > Built-in > NoiseRemover (DSP)**, select **Learn**, and capture two seconds with only background noise present, without speaking or singing.
2. When capture finishes, the **orange** noise estimate stays frozen. **Blue** shows current input, including voice; **purple** shows the actual applied reduction. Use **Relearn** when the noise changes.
3. **Realtime** updates a statistical DSP estimate; it is not AI speech separation. Weak sustained voice may be attenuated. Driver and Receiver buffers add latency separately from this processor.

#### Upgrade and compatibility

Preserve your settings and close DirectPipe yourself before replacing files. After updating the host, review Receiver versions through **Settings > Update Receiver...**. Existing Receiver projects retain their saved buffer choices.

RNNoise and AUTO retain their existing behavior. DPWIM remains a separate, optional external VST; it is downloaded separately through the Windows install action.

#### Verification and limitations

[Detailed verification and limits](https://github.com/LiveTrack-X/DirectPipe/blob/main/docs/RELEASE_4_5_2.md) · [Full CI run](https://github.com/LiveTrack-X/DirectPipe/actions/runs/43801035670)

| Check | Passed | Skipped | Failed |
|---|---:|---:|---:|
| Windows CTest | 825 | 4 | 0 |
| macOS CTest | 710 | 40 | 0 |
| Linux CTest | 710 | 40 | 0 |
| Stream Deck tests | 28 | 0 | 0 |

The exact Windows candidate API checks and native DSP core suites passed. The public source tag **v4.5.2** matches the tested source tree. Skipped checks are recorded separately from passes.

**Windows binaries are unsigned. macOS bundles use ad-hoc signing and are not notarized.** These software results do not establish real vocal/listening quality, physical hardware loopback or prolonged live-session acceptance.

#### VirusTotal

The final distributable files were checked on **2026-11-16 (KST)**. Their SHA-256 values were rechecked after analysis and match the released files.

| File | Detections / checked engines | Unsupported | Failures | Timeouts | Report |
|---|---:|---:|---:|---:|---|
| Windows ZIP | **0/66** | 7 | 1 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/428732cdbe78b03c1f853c25ddaf5537ba553a726edd9115029a933c8ea14134) |
| Windows host EXE | **0/71** | 3 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/ed053918fe6509d62f911425070564e17a9d3cc3d3254375cd99341b94af4036) |
| Receiver VST2 DLL | **0/71** | 3 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/f2876b7c371e008b56776805fd0fb3485d682498904db9f6d45979cad903ed91) |
| Receiver VST3 | **0/71** | 3 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/071dbbb848d55e78e94b892fcff053262df02ac31b6d04cd4e9334bb76cadc09) |
| macOS DMG | **0/61** | 13 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/ebcbc09ba59bad1a2d58775b32e47044a1bcdf1f4eda0f99793f5e02d9f3b63c) |
| Linux tar.gz | **0/62** | 12 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/feb07908b1b4f5c866faa6c41faa37ad04546ca579421f1a61d42476970ac505) |
| Stream Deck | **0/64** | 9 | 1 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/8d2f0d3c41f003a21c56a94c0e9bca993a971eacec88bfca77110a7d98a09f1c) |

Unsupported, failed and timed-out engines are excluded from the checked-engine denominator and listed separately. Windows ZIP and Stream Deck each have one engine failure. Full coverage, analysis times and exact hashes are in the attached reports: [Markdown](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.2/virustotal-report.md) · [JSON](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.2/virustotal-report.json).

Archive scans do not prove exhaustive coverage of every nested file; the three Windows PE payloads also have individual reports. Rebuilt or differently signed files require new exact-byte reports. **No detections does not guarantee that a file is safe.**

#### Downloads and checksums

- `DirectPipe-v4.5.2-Windows.zip`: DirectPipe.exe and Receiver VST2/VST3.
- `DirectPipe-v4.5.2-macOS.dmg`: universal arm64+x86_64 app and Receiver VST2/VST3/AU.
- `DirectPipe-v4.5.2-Linux.tar.gz`: x86_64 app and Receiver VST2/VST3.
- `com.directpipe.directpipe.streamDeckPlugin`: Stream Deck package version 4.5.2.0.
- `checksums.sha256`, `virustotal-report.md`, `virustotal-report.json`: checksums and scan records.

Download the appropriate platform asset and verify the downloaded payload and report files against `checksums.sha256`. DPWIM is a separate download through the Windows install action.

[All changes since v4.5.1](https://github.com/LiveTrack-X/DirectPipe/compare/v4.5.1...v4.5.2)

---

### 한국어

#### 주요 변경 사항

- 수동으로 추가하는 내장 **NoiseRemover (DSP)**를 제공합니다. **Learn(학습)**은 2초 동안 학습한 노이즈 프로필을 고정하며, **Realtime(실시간)**은 노이즈 추정치를 자동으로 갱신합니다. AUTO에는 포함하지 않습니다.
- DSP 편집창에 음성을 포함한 현재 입력, 추정 노이즈 프로필, 주파수별 실제 감쇠량을 표시합니다. 표시 채널은 L / Mono 또는 R을 각각 선택할 수 있습니다.
- 실제 DSP 지연을 샘플 수와 밀리초로 표시하고 체인의 지연 보정에도 반영합니다. **48 kHz에서 256샘플 / 5.333 ms**이며, 학습 중·프로필이 없는 상태·감쇠량 0에서도 같은 지연을 유지합니다. 그래프 표시는 오디오 지연을 추가하지 않습니다. 모드·감쇠량·완료된 수동 학습 프로필은 프리셋에 저장됩니다.
- 새 Receiver 인스턴스의 기본 버퍼를 **Medium(1024프레임)**으로 변경했습니다. 기존 프로젝트에 저장된 버퍼 선택은 유지됩니다.

#### Learn 및 Realtime 사용법

1. **Add Plugin > Built-in > NoiseRemover (DSP)**를 열고 **Learn**을 선택합니다. 말하거나 노래하지 않고 배경 잡음만 있는 상태에서 2초 동안 학습하세요.
2. 학습이 끝나면 **주황색** 노이즈 추정치가 고정됩니다. **파란색**은 음성을 포함한 현재 입력, **보라색**은 실제 적용된 감쇠량입니다. 잡음 환경이 바뀌면 **Relearn**으로 다시 학습하세요.
3. **Realtime**은 통계적 DSP 방식으로 노이즈 추정치를 갱신하며, AI 음성 분리 기능은 아닙니다. 작게 지속되는 음성도 감쇠될 수 있습니다. 드라이버와 Receiver 버퍼의 지연은 이 프로세서의 지연에 별도로 더해집니다.

#### 업데이트 및 호환성

기존 설정을 보존하고 파일 교체 전에 DirectPipe를 직접 종료하세요. 본체 업데이트 후 **Settings > Update Receiver...**에서 Receiver 버전을 확인하세요. 기존 Receiver 프로젝트에 저장된 버퍼 선택은 유지됩니다.

RNNoise와 AUTO의 기존 동작은 유지됩니다. DPWIM은 별도의 선택적 외부 VST이며, Windows의 설치 메뉴를 통해 따로 다운로드합니다.

#### 검증 결과 및 제한 사항

[상세 검증 결과 및 제한 사항](https://github.com/LiveTrack-X/DirectPipe/blob/main/docs/RELEASE_4_5_2.md) · [전체 CI 실행 결과](https://github.com/LiveTrack-X/DirectPipe/actions/runs/43801035670)

| 검사 항목 | 통과 | 건너뜀 | 실패 |
|---|---:|---:|---:|
| Windows CTest | 825 | 4 | 0 |
| macOS CTest | 710 | 40 | 0 |
| Linux CTest | 710 | 40 | 0 |
| Stream Deck 테스트 | 28 | 0 | 0 |

실제 Windows 배포 후보의 API 검사와 네이티브 DSP 코어 검사가 통과했습니다. 공개 소스 태그 **v4.5.2**는 검증한 소스 트리와 일치합니다. 건너뛴 검사는 통과 항목과 구분해 기록했습니다.

**Windows 바이너리는 서명되지 않았습니다. macOS 번들은 임시 서명(ad-hoc)을 사용하며 공증되지 않았습니다.** 이 소프트웨어 검증 결과만으로 실제 보컬·청음 품질, 물리 장비의 오디오 루프백, 장시간 실시간 사용 검증이 완료됐다고 볼 수는 없습니다.

#### VirusTotal 검사 결과

최종 배포 파일을 **2026-11-16(KST)**에 검사했습니다. 검사 완료 후 SHA-256을 다시 대조했으며, 공개된 배포 파일과 일치합니다.

| 파일 | 탐지 수 / 검사 완료 엔진 수 | 파일 형식 미지원 | 검사 실패 | 시간 초과 | 보고서 |
|---|---:|---:|---:|---:|---|
| Windows ZIP | **0/66** | 7 | 1 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/428732cdbe78b03c1f853c25ddaf5537ba553a726edd9115029a933c8ea14134) |
| Windows 본체 EXE | **0/71** | 3 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/ed053918fe6509d62f911425070564e17a9d3cc3d3254375cd99341b94af4036) |
| Receiver VST2 DLL | **0/71** | 3 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/f2876b7c371e008b56776805fd0fb3485d682498904db9f6d45979cad903ed91) |
| Receiver VST3 | **0/71** | 3 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/071dbbb848d55e78e94b892fcff053262df02ac31b6d04cd4e9334bb76cadc09) |
| macOS DMG | **0/61** | 13 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/ebcbc09ba59bad1a2d58775b32e47044a1bcdf1f4eda0f99793f5e02d9f3b63c) |
| Linux tar.gz | **0/62** | 12 | 0 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/feb07908b1b4f5c866faa6c41faa37ad04546ca579421f1a61d42476970ac505) |
| Stream Deck | **0/64** | 9 | 1 | 0 | [VirusTotal](https://www.virustotal.com/gui/file/8d2f0d3c41f003a21c56a94c0e9bca993a971eacec88bfca77110a7d98a09f1c) |

파일 형식 미지원·검사 실패·시간 초과 엔진은 위 검사 완료 엔진 수의 분모에서 제외하고 별도로 표시했습니다. Windows ZIP과 Stream Deck에는 각각 엔진 실패 1건이 있습니다. 전체 검사 범위·검사 시각·정확한 해시는 첨부 보고서에서 확인할 수 있습니다: [Markdown 보고서](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.2/virustotal-report.md) · [JSON 보고서](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.2/virustotal-report.json).

압축 파일 검사만으로 모든 내부 파일이 빠짐없이 검사됐다고 볼 수는 없습니다. Windows PE 실행 파일 3개는 개별 보고서도 확인했습니다. 재빌드하거나 서명이 달라진 파일은 해당 파일의 정확한 바이트를 기준으로 다시 검사해야 합니다. **탐지가 없다는 결과가 파일의 안전을 보증하지는 않습니다.**

#### 다운로드 및 체크섬

- `DirectPipe-v4.5.2-Windows.zip`: DirectPipe.exe 및 Receiver VST2/VST3.
- `DirectPipe-v4.5.2-macOS.dmg`: arm64+x86_64 유니버설 앱 및 Receiver VST2/VST3/AU.
- `DirectPipe-v4.5.2-Linux.tar.gz`: x86_64 앱 및 Receiver VST2/VST3.
- `com.directpipe.directpipe.streamDeckPlugin`: 버전 4.5.2.0의 Stream Deck 패키지.
- `checksums.sha256`, `virustotal-report.md`, `virustotal-report.json`: 체크섬과 검사 기록.

사용할 플랫폼의 배포 파일을 다운로드하고, 내려받은 배포 파일과 보고서를 `checksums.sha256`에 대조하세요. DPWIM은 Windows의 설치 메뉴를 통해 별도로 다운로드합니다.

[v4.5.1 이후 전체 변경 사항](https://github.com/LiveTrack-X/DirectPipe/compare/v4.5.1...v4.5.2)
