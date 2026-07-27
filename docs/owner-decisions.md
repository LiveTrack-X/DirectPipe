# Owner Decisions

Status: Active
Scope: Durable owner authorization and acceptance records

## OD-2026-07-27-0001 - Publish DirectPipe v4.2.1

- Date: 2026-07-27
- Packet: DP-421-RELEASE-0001
- Authority: The owner explicitly instructed Codex to process the v4.2.1
  release and merge it into `main`.
- Authorized actions: commit the v4.2.1 changes, integrate the latest
  `LiveTrack-X/DirectPipe` `main`, push `main`, create and push the `v4.2.1`
  tag, dispatch the repository release CI, and publish the resulting GitHub
  Release.
- Conditions: preserve v4.2.x IPC/VST Receiver/API/Stream Deck compatibility;
  use the checked-in release notes and CI workflow; do not publish incomplete
  assets; do not force-push or move a published release tag. Before publication,
  a failed exact-tag CI attempt may recreate the unpublished tag only after the
  failed SHA/run are recorded and GitHub confirms that no Release exists.
- Accepted evidence limit: release may proceed without a real-device run.
  Windows Voice Clarity/device-specific runtime behavior, third-party VST crash
  containment, installed-package smoke, and macOS/Linux hardware behavior remain
  explicitly unverified and must not be presented as verified.
- Supersedes: The unsatisfied release-publication gate for DP-421-0001 only.
  It does not authorize an incompatible contract change or production service
  deployment.
- Outcome: Accepted and completed. `v4.2.1` was published from
  `258d6ebda559ba39870b611f2d0478e4743969a4` after exact-tag CI run
  `30250688818` succeeded and all published assets matched the release checksum
  manifest. The evidence limits above remain in effect.
