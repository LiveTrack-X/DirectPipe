# Open Implementation Items

Status: Active
Scope: Current implementation backlog only

## Current Priority

1. Preserve the accepted v4.2.1 release evidence and compatibility boundary.
2. Keep architecture-only compatibility changes in their deferred packets.

## Active Work

None.

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

[packet:DP-421-RELEASE-0001] is owner accepted and published. Exact-tag CI,
complete artifacts, checksums, and Critical 0 are recorded in its SPEC. The
release does not establish hardware-compatibility, third-party VST containment,
or production-service claims.

## Recently Closed

- [x] [packet:DP-421-RELEASE-0001] Integrated and published DirectPipe v4.2.1.
  Resolution: `main` and tag source
  `258d6ebda559ba39870b611f2d0478e4743969a4`; CI run `30250688818` succeeded;
  the public latest release contains all four product artifacts plus checksums,
  and downloaded hashes matched the manifest.
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
