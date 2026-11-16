# DirectPipe 4.5.2 release preparation

Status: Published stable 4.5.2; public downloads, checksums and scan identities verified
Date: 2026-11-16
Authority: OD-2026-11-16-0007 / SPEC/DP-452-RELEASE-0001.md; prior preparation OD-0005 (local protocol)

Scope: manual DSP Learn/Realtime and actual spectrum/mask editor; Receiver
Medium default with saved choices retained. Published 4.5.1 is the upgrade base.
The dirty local checkout preserves prior work and missing published workflow/API
latency-test corrections have been reconciled from directpipe/main 481cdda.

## Prior local preparation checkpoint

Windows Release ALL_BUILD succeeded. The staged ZIP has exactly four payload
files: host EXE, Receiver VST2 DLL, Receiver VST3 PE and moduleinfo.json. All
three PE binaries are AMD64 and report file/product version 4.5.2; VST3 metadata
matches. Archive payload hashes equal staging. No installed file was replaced. The staged host is unsigned (NotSigned).

Stream Deck full/production audits report 0 vulnerabilities. Lint, icons, 28 tests,
build, validate and pack pass. The packaged manifest equals source and reports
4.5.2.0 with 10 preserved actions. Native DSP 41/41 and API parser 9/9 pass.
Full Windows CTest: 826 selected, **821 passed, 5 skipped, 0 failed**, 326.90s.
The five explicit skips are the concurrent dispatcher message-loop fixture,
opt-in speech/noise fixture, optional published DPWIM ZIP fixture, optional GForm
VST2 fixture, and multi-instance lock already held by the live app. Online DPWIM
installation into an owned fixture passed. No skip is counted as a pass.
Logs/JUnit: build/release-452/ctest.log and ctest.xml.
Version-only, source/document consistency, UTF-8 and diff whitespace checks pass.
Independent read-only source/metadata/package review approved bounded preparation.

Prepared assets: DirectPipe-v4.5.2-Windows.zip and
com.directpipe.directpipe.streamDeckPlugin; checksums.sha256 covers both.
The Windows ZIP SHA256 is
`16bb697b50b7f91d384cde5c01b7f7bb8f6e01b514e0c6460aa5eb7a4a5655e8`.
The scan manifest/report identifies the ZIP and each of its three binary payloads,
plus the Stream Deck package. All five exact-byte public reports are now complete.

Exact-candidate API smoke was not run: an existing live DirectPipe process
(PID 18688) owns 8765/8766. It was neither stopped nor used as the test candidate.


Current outputs/logs belong to `build/release-452/`. Earlier DSP feature checks
remain under `build/dsp-nr-spectrum-evidence/`; those binaries are 4.5.1-versioned
checkpoints, not 4.5.2 release files. Final checks/results are recorded here after
execution. Baseline working files are preserved in preparation-baseline.

## Prior local VirusTotal checkpoint

Prior local 4.5.2 files checked on 2026-11-16 (KST); exact SHA-256 identities are
recorded in `virustotal-report.json` and `virustotal-report.md`.

| File | Detections / checked engines | Unsupported | Engine failures | Report |
|---|---:|---:|---|---|
| Windows ZIP | **0/67** | 7 | 0 | [Report](https://www.virustotal.com/gui/file/16bb697b50b7f91d384cde5c01b7f7bb8f6e01b514e0c6460aa5eb7a4a5655e8) |
| DirectPipe.exe | **0/71** | 3 | 0 | [Report](https://www.virustotal.com/gui/file/e6f2f861f6b8446f1e37077ae8585a25b82d18b0dd56c82a74610bcdcbf04b21) |
| Receiver VST2 DLL | **0/71** | 3 | 0 | [Report](https://www.virustotal.com/gui/file/f3b8a276a1f03500e659066b73ff897a4f7ff8b57a9f20aab2c40bec2bb25b9a) |
| Receiver VST3 | **0/71** | 3 | not separately captured | [Report](https://www.virustotal.com/gui/file/898b6c59fbf45c65748707b4c61ec1656c75653adc8a5b1ed4cccec75a48b80a) |
| Stream Deck package | **0/64** | 9 | 1 | [Report](https://www.virustotal.com/gui/file/97fc14d1170b5d13729478a26093d2703a45e0d3833769995864e5567ba22d13) |

검사 완료 후 로컬 파일의 SHA-256을 다시 대조했습니다. 위 비율에서 검사 불가·실패
엔진은 제외하며, Stream Deck에는 엔진 실패 1건이 있습니다. ZIP 내부 파일을
일괄 검증했다고 해석하지 않도록 세 PE 실행 파일도 각각 확인했습니다.

These are point-in-time vendor results, not a safety guarantee. All final local
hashes still match their reports. Rebuilt/signed or platform-specific packages
need their own exact-byte reports; macOS/Linux results are not claimed here.

Detailed local evidence: build/release-452/virustotal-report.json and .md.

## Authorized release execution and remaining gates

- Windows/macOS/Linux/Stream Deck source-version CI passed; see the final checkpoint below.
- Real vocal/listening, hardware loopback and long-session deadline acceptance.
- OD-2026-11-16-0007 now authorizes commits/PR/merge/tag/CI/draft/publication after gates.
- The earlier preparation scans above apply only to their exact local hashes. Final
  CI/distributable bytes require matching or fresh reports before publication.
- The workflow stages a draft only; final promotion follows complete artifact verification.
- Live app/device/config and installed plugins remain untouched.

### Pre-publication review corrections

The source/document audit is recorded in DOCUMENTATION_AUDIT_4_5_2.md. Added
native core CTest coverage on all CI platforms, completed-Learn concurrent save,
async learned-profile persistence and restoration of all five Receiver choices.
The workflow now stops at draft and checks exact Windows archive members/AMD64/
VST3 metadata before staging. Local focused native 41, DSP/preset15 and Receiver2
checks passed after these coverage changes.

The first PR CI run43800434529 exposed a Linux LP64 compiler ambiguity in the
DPWIM asset-size JSON conversion at e5c7d20. Restored the exact juce::int64
conversion (int64_t may be long on LP64) and clarified its comment. Fresh platform
CI is required; the failed run is retained and is not counted as validation.

### Final source and CI checkpoint

PR11 merged as `6f0fbd3110d52101faa704c8c098050187a5a868` and is the immutable
v4.5.2 source tag. Full CI 43801035670 tested PR head a5fe0c2 via test merge
634166c8; its Git tree `d8d105379cff7445d7c816311eb631dc5522a2e6` exactly matches
the merged/tagged tree. The validated CI artifacts are promoted unchanged.

| Gate | Passed | Skipped | Failed |
|---|---:|---:|---:|
| Windows CTest |825|4|0|
| macOS CTest |710|40|0|
| Linux CTest |710|40|0|
| Stream Deck tests |28|0|0|
| API parser |9|0|0|

The native 41-check core executable is included in each platform's CTest total
as one registered test. Windows exact-candidate API smoke passed and gracefully
closed its own process; its EXE SHA256 matches the final Windows ZIP payload.
The local live app was not contacted. Explicit platform/optional fixture skips
remain in JUnit and are not passes. Full/production npm audits report 0 issues.

Final Windows archive has exactly 4 payload files, 3 AMD64 binaries with 4.5.2
file/product versions and matching VST3 metadata. Windows binaries are unsigned;
repository secrets contain SDKs but no signing certificate. macOS app/VST2/VST3/
AU bundle versions and arm64+x86_64 architectures passed CI; verified mounted
DMG payload matches staging. Linux 3 ELF x86_64 binaries and tar payload passed
identity checks. Stream Deck manifest 4.5.2.0/10 action UUIDs matches source.

Draft downloads of all 4 payloads and the preliminary checksum file match the
validated originals. Final VirusTotal analysis and final notes/reports/checksums
are held before public promotion. This section is a software/artifact checkpoint,
not yet a public-download claim or hardware/listening acceptance.

### Final distributable scans and pre-publication gate

All 7 final CI file reports completed and post-scan hashes match. Exact final
identities/engine coverage/timestamps are in the release assets
`virustotal-report.json` / `.md`; these supersede the earlier local checkpoint
for public-distribution claims. The final Windows ZIP SHA256 is
`428732cdbe78b03c1f853c25ddaf5537ba553a726edd9115029a933c8ea14134`.

### VirusTotal

Final distributable bytes checked on2026-11-16 (KST). Post-scan SHA-256 values
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
Deck에 각각 엔진 실패1건이 있습니다. 전체 범위·시각·정확한 해시는 첨부한
`virustotal-report.md` / `.json`에 기록했습니다. 무탐지는 안전 보증이 아닙니다.

Unsupported/failed/timeout engines are recorded separately. Archive scans do not
prove exhaustive coverage of every nested file; the three Windows PE payloads
also have individual reports. Rebuilt or differently signed bytes need new reports.


Pre-publication review verified all seven release assets (four payloads, two
reports and checksums) through private draft downloads. Public promotion and
independent public-download verification are recorded after execution. No live
user app/device or installed plugin was changed.

## Public publication and independent verification

Published stable release: https://github.com/LiveTrack-X/DirectPipe/releases/tag/v4.5.2

Publication time: 2026-11-16T13:33:29+09:00. Latest stable release selects 4.5.2;
release ID 446600830 is public, non-draft and non-prerelease. Source tag remains
`6f0fbd3110d52101faa704c8c098050187a5a868`; subsequent main commits update
only documentation/evidence, without changing the validated source tag or assets.

All seven assets were downloaded again through anonymous public URLs. Their
bytes match validated staging; all six checksum entries match, and all seven
VirusTotal file identities match public archive bytes or Windows archive members.
Public notes match the final reviewed Markdown after GitHub line-ending
normalization. Windows exact-member/AMD64/VST3 identities, Stream Deck manifest,
Linux ELF/tar and DMG container checks passed on those downloaded files. macOS
universal bundle and mounted payload checks passed on the CI runner.

Public scan records:
[Markdown report](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.2/virustotal-report.md),
[JSON report](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.2/virustotal-report.json),
[checksums](https://github.com/LiveTrack-X/DirectPipe/releases/download/v4.5.2/checksums.sha256).

Detailed reproducible evidence is retained under build/release-452-publication:
source-artifact-provenance.json, ci-test-summary.json, final raw VirusTotal GUI
snapshots/summaries, public-verification.json and downloaded archive-verification.
No live installed app/device/configuration or installed Receiver was changed.
Actual vocal/listening, hardware round-trip and prolonged deadline acceptance
remain separate; Windows unsigned/macOS ad-hoc/not-notarized limits remain in notes.

### Complete English/Korean release-note correction

The public description and current repository release notes/changelog now have
full English and Korean text for the same changes, usage, upgrade, verification,
limitations, scan results and downloads. Both language sections contain the
same links, file identifiers and test/scan figures. The live release body was
retrieved and matched to source after newline normalization. The release ID,
tag, publication timestamp and all seven asset IDs/sizes/digests are unchanged.

공개 릴리스 설명과 현재 저장소의 릴리스 노트·변경 이력을 영어·한국어 전체
본문으로 맞췄습니다. 변경 사항·사용법·업데이트·검증·제한 사항·검사 결과·
다운로드 안내의 내용과 링크·파일명·수치를 두 언어에서 대조했습니다. 공개된
본문도 원문과 일치하며 릴리스 ID·태그·공개 시각·첨부 파일 7개의 ID·크기·
해시는 그대로입니다.
