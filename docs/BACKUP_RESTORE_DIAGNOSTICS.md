# Backup restore diagnostics

Date: 2026-11-11
Status: Local implementation and verification; not a published release

## Requested behavior

When loading a settings (`.dpbackup`) or full (`.dpfullbackup`) backup fails,
identify the failed stage, reason and what the user should check. Preserve
existing backups, settings and restore transactions across updates. No failing
owner backup was supplied, so this work addresses missing diagnostic feedback
rather than claiming a cause for a particular private file.

Previously, many parse/staging failures returned only `false`; the load dialog
then displayed the same generic message. A selected missing file could return
silently, and a failed read could become empty text. Async plug-in errors could
appear only in a transient notification.

## Resulting feedback

Each operation has a `SettingsExporter::RestoreDiagnostics` report. It reaches
the dialog and log through the same restore operation, with optional output
arguments preserving existing bool import results and callbacks.

| Stage | What to check |
| --- | --- |
| Selected file / read / empty content | File still exists, is readable, and is the intended intact export |
| JSON / backup version or section structure | Select the correct backup or export an intact copy again |
| Platform | Restore on the same OS; legacy backups without platform retain their previous acceptance |
| Runtime busy or unstable | Let the current preset load finish or resolve the partial load before retrying |
| Snapshot / temporary staging | Access to current settings/slots and writable temporary storage |
| Audio and plug-in restore | Earlier loader/device log messages; plug-in installation, format and architecture |
| Control / slot commit | Writable settings location, file locks and slot file access |
| Observed incomplete rollback | Inspect current audio settings, controls and slots before continuing |

File chooser cancellation remains quiet. A failed operation reports actionable
feedback; successful imports do not become failures merely because an existing
device warning occurred. If a lower layer does not identify the exact cause,
the report says audio/plug-in restoration failed and points to the earlier log;
it does not invent a missing plug-in or device diagnosis.

Async plug-in preparation also writes a bounded, single-line plug-in label and
loader error to the log before the existing notification callback. Diagnostics
do not dump backup JSON, saved plug-in state or control values. Reports are
operation-specific, never a global last-error variable or replacement callback.

## Compatibility

Backup serialization, legacy missing-platform behavior, accepted positive
version values, settings-only exclusion of chains/slots, full restore ordering,
snapshot/rollback and existing warning semantics are preserved. The application
still claims the existing load gate and releases it through the prior success/
failure paths. This change adds explanations rather than rewriting old backups.

The separate future Analog 1/2 source split is proposal-only. It must be opt-in
and preserve existing Analog 1+2 bindings, Mono/Stereo, gains, presets and
external-control identities. No multi-input implementation is included here.

## Verification

Seven diagnostic regressions failed against the initial scaffold because the
operation reports were empty while the existing failure paths executed. They
cover malformed input/legacy compatibility, invalid audio settings, unstable
runtime, control-write failure, slot prevalidation, late slot failure/rollback
and async plug-in preparation failure.

- Windows Release application/host-test target builds successfully.
- SettingsExporter suite: 31 passed, 0 failed; includes report clearing after a
  successful legacy import, safe log content, an actual incomplete rollback
  fixture and specific async commit failure surviving fallback reporting.
- Final focused engine/preset/settings/VST run: 238 passed, 1 optional GForm
  test skipped, 0 failed. The real GForm test passed separately for versions
  3.1 and 1.2 against the final executable.
- Independent source review found no actionable issue in legacy acceptance,
  transaction order, report lifetime, gate cleanup or the dialog/log paths.
- Final Windows Release CTest: 669 registrations, 666 passed, 3 skipped,
  0 failed (232.46 seconds). Skips are the environment-dependent concurrent
  dispatch and already-held multi-instance checks, plus optional external GForm.
  Separate real GForm runs passed for both official DLLs against this build.
- UTF-8/text integrity, patch whitespace and strict SDAD Doctor checks pass.

Local evidence is under `build/test-results/` and `build/backup-diagnostics-*`.
The prior mono-routing checkpoint is recorded independently in
[`MONO_VST_ROUTING.md`](MONO_VST_ROUTING.md).

No interactive native file chooser, physical device or owner's private backup
was used. Dialog wiring is compiled/source-reviewed; automated fixture tests
establish only the failure/compatibility cases they exercise. No installed
application, release asset, tag, remote comment or device setting was changed.
Internal async PresetManager audio/chain rollback outcomes are not exposed at
this reporting boundary. Earlier logs remain the source for those details;
the fallback does not assert that every previous setting was restored.

Read for these changes: `AGENTS.md`, state and `docs/INDEX.md`; the active packet
SPECs; targeted owner decisions/TODO/findings and operating-rule authority/
project constraints; `TESTING.md`; affected Audio/UI module READMEs; targeted
`CLAUDE.md` thread/lock/build patterns and product/architecture/user guides.
