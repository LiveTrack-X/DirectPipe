# 4.5.0 documentation and comment audit / 문서·주석 점검

Date: 2026-11-12 (Asia/Seoul)

This records the final documentation follow-up. The release binaries are bound
to tag `v4.5.0`, source `89db71239ff6607f3a7e5561dce682ac298804bc` and
[exact-source CI](https://github.com/LiveTrack-X/DirectPipe/actions/runs/43078129309).
Publication, public-download checks and remaining acceptance limits belong to
the [verification record](RELEASE_4_5_0.md). Documentation follow-ups do not move
that tag or replace its binary assets.

현재 문서는 최종 구현·검증 근거에 맞췄으며 과거 릴리즈·체크포인트 기록을 보존했습니다.
자동 검사, 공개 파일 확인, 실제 장치·청취 수락은 서로 구분합니다.

## Current documentation / 현재 문서

| Documents | Reconciliation |
| --- | --- |
| [README](../README.md), [Quick Start](QUICKSTART.md), [User Guide](USER_GUIDE.md) | Current 4.5.0 entry points, single-input opt-in, native one-input compatibility, default-off system-wide mic option and current Receiver upkeep |
| [Product spec](PRODUCT_SPEC.md), [Architecture](ARCHITECTURE.md) | Physical capture versus Mono/Stereo DSP, main-bus mono fanout, exact-endpoint non-RT keeper, restore gates and idle autosave/sidecar semantics |
| [Backup diagnostics](BACKUP_RESTORE_DIAGNOSTICS.md), [Update recovery](WINDOWS_UPDATE_RECOVERY.md), [Logging](LOGRULE.md) | Stage/reason/next-check messages, incomplete rollback, separate replacement/startup/cleanup outcomes, managed-backup retention and old-updater first-upgrade limits |
| [Testing](../TESTING.md), [Building](BUILDING.md), [Contributing](../CONTRIBUTING.md) | Tracked API client and isolated candidate helper, required coverage, actual skip reasons, fake mic tests, native package checks and current CI runners |
| [Platform guide](PLATFORM_GUIDE.md) | Current Windows/macOS/Linux build paths, universal macOS 10.15 target, feature support and hardware limits |
| [Control API](CONTROL_API.md), [API examples](API_EXAMPLES.md) | Additive `audio_running`, finite 0–2 input gain, separate main/monitor latency estimates without callback-duration double counting, and preserved action/Receiver semantics |
| [Security policy](../SECURITY.md) | Current published-version metadata and private reporting channel; existing support grades retained, without claiming a full security audit |
| [Stream Deck](STREAMDECK_GUIDE.md), [Third-party licenses](../THIRD_PARTY.md) | 4.5.0.0 identity, maintained esbuild tooling, exact vendor patch provenance and five packaged runtime license notices |
| [Auto design](AUTO_DESIGN.md), [AutoGain extraction](AUTOGAIN_PLUGIN_GUIDE.md) | Auto does not change the mic-level preference; standalone AutoGain remains a design proposal; current runner example |
| [Mono routing](MONO_VST_ROUTING.md) | Historical real-GForm/checkpoint evidence retained, with current single-input context and future concurrent-chain work kept distinct |
| [Release note](ReleaseNote.md), [Changelog](../CHANGELOG.md), current/versioned release bodies | Current feature/compatibility notes, prior bilingual format, native one-channel exception and matching current body copies; older entries retained |

Local operating guides and the project guide were reconciled with the same
current-source facts. The IPC module guide was reviewed: transport ABI,
eight independent queues and the separate legacy path are unchanged in 4.5.0.
Previous version reports and archived proposals retain their original scope,
counts and acceptance limits.

## Code comments / 코드 주석

- Input selection describes a contiguous range with a default pair and explicit
  single physical input; output selection also describes its actual range API.
- Default input-pair recovery is distinguished from the independent capture
  selection and Mono/Stereo processing mode.
- Mono averaging avoids increasing coherent identical L/R gain; the incorrect
  numeric boost description was removed.
- Install-only fixtures disable relaunch. Startup fixtures intentionally launch
  only their owned, audio-free probe executables.
- Main-bus/auxiliary routing, endpoint identity, worker/write lifetime gates,
  ACK atomic commit, restore rollback and shutdown ownership comments were
  checked against current source and tests.

## Evidence and limits / 검증 범위

UTF-8 integrity, patch whitespace, current version metadata, release-body copy
equality and changed local documentation links are checked separately from the
native CI gates. Historical test-count tables are labelled as checkpoints.
The existing 22 unchecked manual test markers remain unchecked.

Real GForm DLL rendering covers the processing graph, not the reporter's complete
WASAPI/VB-Cable setup. Mic-level tests use fake endpoints and do not change the
user's actual microphone level. Startup tests launch audio-free stand-ins;
physical drivers, listening, native choosers/private backups and interactive UAC
still require their own evidence. Multiple simultaneous microphone chains are
future work; single-channel capture does not claim that feature.
