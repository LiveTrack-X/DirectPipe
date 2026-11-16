# DirectPipe 4.5.2 release preparation

Status: Authorized public release execution; final exact-source CI and artifact checks in progress
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

- macOS/Linux builds/tests and source-version CI before all-platform claims.
- Real vocal/listening, hardware loopback and long-session deadline acceptance.
- OD-2026-11-16-0007 now authorizes commits/PR/merge/tag/CI/draft/publication after gates.
- The earlier preparation scans above apply only to their exact local hashes. Final
  CI/distributable bytes require matching or fresh reports before publication.
- The workflow stages a draft only; final promotion follows complete artifact verification.
- Live app/device/config and installed plugins remain untouched.
