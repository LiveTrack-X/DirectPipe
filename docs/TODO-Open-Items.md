# Open Implementation Items

Status: Active
Scope: Current implementation backlog only

## Current Priority

1. Complete [packet:DP-421-RELEASE-0001] by binding the exact commit, tag, CI
   run, artifacts, checksums, and publication evidence.
2. Keep architecture-only compatibility changes outside this completed packet.

## Active Work

- [ ] [packet:DP-421-RELEASE-0001] Integrate the latest `directpipe/main`, push
  `main`, create and push `v4.2.1`, dispatch exact-tag CI, and verify the complete
  public release. Owner authorization: `OD-2026-07-27-0001`.

## Future / Deferred

- `DP-AUTH-0001`: authenticated loopback control contract; revisit only with an
  owner-approved API/Stream Deck migration boundary.
- `DP-RESAMPLE-0001`: real-time Receiver sample-rate conversion; revisit when
  same-rate operation is no longer an acceptable compatibility contract.
- `DP-VST-SANDBOX-0001`: separate-process VST hosting/crash isolation; revisit
  as an architecture project with latency and state-transfer acceptance tests.
- `DP-IPC-MAPPING-0001`: Windows retained-mapping handover for stopped older
  Receivers; revisit with a protocol/lifetime migration design.

## Release / Production Readiness

[packet:DP-421-RELEASE-0001] is authorized to publish v4.2.1 without a real-device
run, subject to exact-tag CI, complete artifacts, checksums, Critical 0, and the
claim limits recorded in its SPEC. It does not authorize hardware-compatibility,
third-party VST containment, or production-service claims.

## Recently Closed

- [x] [packet:DP-421-0001] Audio/RT unit: noise removal, recorder lifecycle,
  writer accounting, VST graph lifecycle, and focused regression tests.
  Resolution: fixed and software verified in the integrated Release build.
- [x] [packet:DP-421-0001] Persistence/UI unit: reset/clear cancellation,
  transactional preset/backup operations, recording-folder state, durable
  failure handling, safe callbacks, and MIDI learn concurrency.
  Resolution: fixed; focused persistence tests and full CTest passed.
- [x] [packet:DP-421-0001] IPC/control/CI unit: Receiver generation and RT
  safety, preset state, updater integrity/errors, release ordering, and SDK
  gates. Resolution: fixed in source with regression coverage; remote release
  CI remains a future release-packet evidence gate.
- [x] [packet:DP-421-0001] Integration unit: v4.2.1 version/docs alignment,
  Release build, 484 registered tests, Stream Deck validation, SDAD Doctor,
  text integrity, metadata, and diff hygiene. Resolution: software verified.
- [x] [packet:DP-SDAD-0001] Installed the Full SDAD v3.2.2 control plane.
  Resolution: software-verified. Evidence: pinned adapter SHA-256 matched;
  Doctor 3.2.2 strict reported 0 errors and 0 warnings; DirectPipe text
  integrity and `git diff --check` passed.
