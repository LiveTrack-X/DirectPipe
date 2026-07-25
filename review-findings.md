# Review Findings

Status: Active
Scope: Active bugs and review findings only

## Active Findings

None currently tracked for the software-verified DP-421-0001 scope.

Do not leave closed findings in this section. Move fixed or accepted items to
`## Recently Closed` before an evidence checkpoint or handoff.

## Future / Deferred Findings

- [Medium] [packet:DP-AUTH-0001] Loopback HTTP/WebSocket control remains
  unauthenticated. Evidence: current clients rely on the v4.2.x loopback payload
  contract. Deferral reason: mandatory authentication changes API/Stream Deck
  behavior. Revisit trigger: owner approves a compatibility migration packet.
- [Medium] [packet:DP-RESAMPLE-0001] DirectPipe and Receiver still require the
  same sample rate. Evidence: v4.2.x Receiver has no real-time resampler.
  Deferral reason: DSP/latency architecture change. Revisit trigger: cross-rate
  operation becomes an owner requirement.
- [Medium] [packet:DP-VST-SANDBOX-0001] Loaded third-party VST processing is not
  isolated in a separate host process. Evidence: the scanner is out of process,
  but live plug-in DSP remains in the app. Deferral reason: process, latency,
  state-transfer, and UI architecture change. Revisit trigger: crash containment
  becomes an explicit product requirement.
- [Medium] [packet:DP-IPC-MAPPING-0001] A stopped older Receiver can retain the
  Windows shared-memory mapping; a new v4.2.1 host waits briefly and fails closed
  rather than overwrite it. Deferral reason: safe handover needs a protocol or
  lifetime migration. Revisit trigger: owner approves an IPC compatibility packet.

## Severity Gate

- Critical findings block release or production readiness.
- High-risk domain findings block the affected slice until reviewed and tested.
- Release candidates should reach Critical 0 before owner acceptance.

## Recently Closed

- [High] [packet:DP-421-0001] Fixed noise-removal FIFO/latency, recorder
  publication/accounting, and VST graph lifecycle races. Evidence: focused
  regression suites plus integrated Release build and 484-test CTest run.
- [High] [packet:DP-421-0001] Fixed reset/clear stale completion, durable
  autosave/slot failures, and transactional preset/full-backup restore.
  Evidence: persistence-focused 95/95 and final 10/10 runs plus full CTest.
- [High] [packet:DP-421-0001] Fixed Receiver producer-generation reconnect,
  callback-safe connection retirement, and off-callback latency notification.
  Evidence: core/host regression coverage and integrated static review.
- [High] [packet:DP-421-0001] Fixed release source ordering and VST2/ASIO SDK
  fail-closed gates. Evidence: workflow regression tests, YAML parsing, and
  source review; remote CI/publication remains owner-gated release evidence.
- [Medium] [packet:DP-421-0001] Fixed recording-folder playback, filename
  collisions, duration accounting, recording backup/reset, deletion reporting,
  UI lifetime, preset API state, updater integrity/errors, settings errors, MIDI
  learn races, and v4.2.1 documentation alignment. Evidence: focused tests,
  Stream Deck validation, and full CTest.
- [Medium] [packet:DP-421-0001] Fixed the event-signaled IPC test's unbounded
  Windows auto-reset-event assumption by draining queued blocks and enforcing a
  deadline. Evidence: 100/100 stress iterations and final full CTest passed.

## Guardrails

Stop feature work if critical tests fail, security boundaries regress, or production-readiness evidence is missing.
