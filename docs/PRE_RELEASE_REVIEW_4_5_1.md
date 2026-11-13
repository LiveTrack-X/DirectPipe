# DirectPipe 4.5.1 pre-release code review

Date: 2026-11-13
Baseline: bca449916351a436284816f3555f7e43641a70e7.
Scope: changed Audio/Control/UI/source files and final installer/packaging paths.

The nine project categories were reviewed by module: thread/lifetime safety,
device state/fallback, real-time callback safety, lock order, persistence,
async chain replacement, modal UI, server protocol, and version/Stream Deck sync.
Review covered new files and surrounding lifecycle/load paths, with complete
module reads rather than only diff fragments. It is static review, not hardware
acceptance or proof against all defects.

## Final source findings

- Input bars previously measured the Mono duplicated work buffer. Separate
  pre-Mono capture measurements now preserve source independence and scalar API
  behavior; callback regression fixtures cover both one-sided directions.
- RNNoise conversion uses prepared state, bounded clocks/queues, independent
  channels and correct native/converted PDC. No confirmed remaining introduced
  audio callback allocation/lock/lifetime defect was found in the reviewed scope.
- Targeted installer scans initially replaced unrelated plugin-cache entries.
  Results now merge; only superseded same-identity managed DPWIM cache entries
  are retired on successful scans. External plugins/active instances are retained.
- Concurrent installers could publish an older manifest after a newer version
  completed during download. Final commit now serializes across processes and
  rechecks installed version. A deterministic download-interleaving test covers it.
- ZIP entry separators are normalized before validation, including JUCE's
  Windows separator conversion. SHA-256, paths, names, counts/sizes, duplicate
  identities and bundle/PE versions are checked before publication.

## Deferred integration

The built-in DPWIM experiment is excluded by the owner's final direction.
Its stale-PID restore, ADV attachment lifetime and unsupported-platform preset
repair findings/fixes are preserved in the deferred snapshot. They are not
claimed fixed in the separate published DPWIM 0.3.4 binary. Current documentation
and installer treat it as an optional external preview VST3.

## Release recommendation

Final code-review findings above are addressed. Release remains conditional on
current local regression, original full CI/version/audit/API/package checks,
accurate documentation and public asset verification. Local sandbox failures
and missing hardware/listening evidence must remain explicit. See
[release verification](RELEASE_4_5_1.md) and [external installer setup](DPWIM_WINDOWS_SETUP.md).
