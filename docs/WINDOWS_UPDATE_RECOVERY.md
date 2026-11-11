# Windows update diagnosis and recovery (4.5.0)

Date: 2026-11-12
Status: 4.5.0 recovery behavior; verification and acceptance limits are recorded in [the release verification record](RELEASE_4_5_0.md).

## Comparison with previous releases

The v4.3.0 and v4.4.0 host-only wait/install/completion helper source was
identical after line-ending normalization. Version 4.4.0 added a separate
Receiver maintenance transaction. An upgrade **to** 4.4.0 is initiated by the
updater in the older running executable. The newly downloaded executable
cannot repair that earlier installer before it runs.

### Earlier update behavior (follow-up comparison)

The owner's recollection that the old EXE disappeared after startup is correct.
The earlier comparison of 4.3.0 with 4.4.0 did not cover the earlier cleanup
policy change:

| Tagged versions inspected | Startup backup cleanup | Parent wait / host restart |
| --- | --- | --- |
| 3.8.0, 3.9.0/3.9.12, 3.10.3, 4.0.0, 4.1.2 | Deletes `DirectPipe_backup.exe` on startup | Waits by `DirectPipe.exe` image name; restarts with PowerShell `Start-Process` |
| 4.2.0, 4.2.6 | Still deletes the backup on startup | Waits for the launching PID; same `Start-Process` restart |
| 4.2.7, 4.2.8, 4.3.0, 4.4.0 | Retains the backup for the next update rotation | Launching-PID wait and `Start-Process`; 4.4.0 adds the separate Receiver transaction |

Commit `e92b8c76f52d7864de71563c13573acf299a7aad` (4.2.7) removed the
startup backup deletion and changed its regression test from
`SuccessfulStandaloneUpdateKeepsBackupUntilNextStartup` to
`SuccessfulStandaloneUpdateKeepsBackupForNextRotation`. Thus backup retention
was an intentional policy change, rather than proof that startup failed.
Commit `7914d79f9f50475ca9a70d8c410040eb0c6470d3` (4.2.0) introduced the
launching-PID wait instead of waiting for every `DirectPipe.exe` process.

On the inspected installation, `DirectPipe.exe` is 4.4.0 and
`DirectPipe_backup.exe` is 4.3.0. Other date-suffixed `.backup-*`/`.bak-*` files
are older 4.0.x copies and were not targets of that named-backup cleanup.
Downloaded/staged `DirectPipe_update.exe`, ZIP, BAT and `_update` are separate
temporary files. None of these installed files was deleted or changed.

Backup retention and automatic restart are separate results. The historical
`Start-Process` call did not verify application readiness. The 4.5.0 updater
uses desktop-context relaunch and records completion errors, as described
below. A read-only COM probe resolved this Windows desktop/application object;
it launched no process and does not establish real app restart readiness or
identify the owner's earlier restart failure. That history-only comparison
changed no code or binaries; the subsequent OD-0006 implementation is below.

The installed directory name suggests 3.7.0, but the actual current EXE has
file/product version 4.4.0 and matches the retained public 4.4.0 download.
Its backup EXE is 4.3.0. These files and the running application were not
changed. Their presence does not establish whether the historical update
finished normally or required manual recovery. No retained error identifies
the owner's original failure stage, so its cause remains unconfirmed.

## Controlled old-updater observations

An isolated TEMP harness mechanically evaluated the original C++ BAT template;
it did not call a compiled old helper or launch the old application.

- A copied real 4.3.0 EXE upgraded to the verified public 4.4.0 ZIP successfully.
- A locked synthetic current EXE caused replacement failure. The old helper
  deleted an existing backup before failing and left only console feedback.
- Waiting for an owned fixture PID had no deadline. It ended when the fixture
  process exited; no installed application was stopped.

These demonstrate concrete failure and diagnostic gaps, rather than the cause
of the owner's historical incident. Evidence is retained under
`build/release-4.5.0-evidence/` and the fixture paths listed in its reports.

## 4.5.0 behavior

The updater retains the Windows BAT host installer, strict release/version
checks, ZIP/standalone host support and the separate Receiver transaction.
It adds a bounded wait for the launching PID, same-volume host replacement,
preservation of a pre-existing backup, rollback verification and durable
stage/error/recovery diagnostics. It never terminates another audio program.

Under OD-2026-11-11-0006, confirmed initial startup now precedes automatic
cleanup of known previous host EXEs. File replacement, application readiness
and cleanup are separate results. Readiness follows initial settings/plugin
restore and preload completion, then the saved window/tray policy. Loading,
partial or unstable restore and a failed window policy cannot acknowledge ready.
The inspected installed preference has tray startup enabled; that can hide a
successful launch, but does not establish the historical incident's cause.

New host/paired installers pass only a unique attempt token. The app derives
its acknowledgement path from its own per-install receipt; no arbitrary CLI
file path is accepted. The app writes through a short sibling temporary file,
checks write/flush/close and commits atomically without replacing an existing
acknowledgement; valid long destination paths do not gain a second long basename.
The installer checks token, version, executable path,
the acknowledging PID's actual image, candidate hash and a 500 ms liveness
interval. The default acknowledgement wait is 60 seconds. It durably saves
ready evidence before any backup deletion, validates captured backup hashes,
then deletes the older prior backup before the most recent host backup.
Receiver-only maintenance and Receiver recovery backups retain their contract.

Missing, stale, foreign, negative or dead-process acknowledgement keeps the
committed candidate and host recovery files; it never rolls back underneath
a possibly running candidate. Ready-but-incomplete cleanup is reported as
cleanup failure; a later flag/result failure is reported as completion failure.
The ready app polls finalization, consumes its success flag and notifies only
after the final verified result. The installer reports errors when no ready
application is available to display them.

For old flag-only updaters, the new app waits for the same ready boundary and
requires a matching version flag, no pending receipt/token, real DirectPipe PE
identity and exact SHA-256 checks. Only `DirectPipe_backup.exe` (strictly older)
and redundant `DirectPipe_update.exe` (same/older) are eligible. Staged cleanup
runs before the newest backup and stops on failure. Exclusive handles bind
Windows deletion to the verified file; reparse paths, unverified identities,
locks and permissions cause retention with an actionable warning. No flag
means no automatic legacy EXE deletion. Dated/manual backups and settings stay.

Download, script and log preparation use a unique caller-writable TEMP folder.
Replacement staging is beside the installed EXE, including when elevation is
needed. The app closes only after installer launch succeeds; cancelling UAC
leaves it running. Real application GUI/audio startup and protected-folder UAC
still require interactive acceptance; harmless TEMP fixtures do not prove them.

Each executable path has its own receipt under
`%APPDATA%/DirectPipe/update-results/<path-sha256>.json`; Windows path case does
not create another identity. The unique TEMP work folder contains `update.log`.
The failure dialog identifies these locations and retained recovery files.
Replacement success and completion/restart failure are distinct outcomes.
On startup, actionable results are shown until the warning is dismissed;
acknowledging them retains the receipt and recovery files. A later updater's
changed receipt is not overwritten by that acknowledgement. A target version
older than the running version is treated as superseded.

The focused suite passed 57/57: 10 real desktop-launch host cases, 2 paired
cases, 13 startup-support cases, 21 existing host script cases and 11 receipt
decisions. The extension first failed 19 of 23 initial checks, before production
implementation; two later cases cover native PE inspection and late ready-
receipt I/O failure. The consoleless Win32 stand-in links no JUCE/audio code,
and is used for both old and new fixture hosts, including rollback paths.
It exercises the actual desktop COM launcher and actual process-image checks;
it is not the real DirectPipe GUI. Notifications are suppressed in fixtures.
These counts describe the startup-confirmation checkpoint. Current combined
results and the final workflow gate are in the [verification record](RELEASE_4_5_0.md).

The 4.5.0 ZIP from that local checkpoint also passed the frozen 4.3.0/4.4.0 legacy template's
install-only path in TEMP. It installed the exact archived host, retained the
copied 4.3.0 EXE as backup and preserved synthetic configuration markers. This
demonstrates that package's normal old-updater path; it cannot repair old
updater failure handling or establish the owner's historical cause. It was a
mechanically evaluated template, not an executed historical application.

## If an older updater stops

Keep the current EXE and any backup/recovery files. Capture the exact error and
whether it stopped at download, waiting, replacement or restart. Check whether
the application is still running and whether the destination is writable or
locked. Do not remove backups to retry.

For manual recovery, obtain the exact Windows ZIP and `checksums.sha256` from
the same official release, verify the archive, close DirectPipe yourself and
extract the host to a new folder first. Preserve the existing settings and
Receiver installations. Avoid overwriting a loaded Receiver; close its using
application before Receiver maintenance. A fresh-folder host can confirm the
download without deleting the previous installation.

No real installation, UAC approval, DirectPipe GUI launch or public release was performed
by this work. See the verification record for exact automated results and limits.
