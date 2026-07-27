# DirectPipe Integrated SPEC

Status: Canonical integrated SPEC
Scope: Current product and implementation baseline

`COMPLETE` means integrated baseline, not immutable or automatically active.
For a stateful packet, `sdad-state.yaml#active_spec` selects the single
normative SPEC entrypoint.

## SPEC Authority And Lineage

This integrated baseline is not immutable, and it is not automatically active;
state selects the normative entrypoint.

An additional SPEC does not become authority merely because it is newer, has
`FINAL` or `COMPLETE` in its name, or exists under `SPEC/`. Requested action and
owner intent matter: a SPEC supplied as current requirements is a change
request, while review/draft/reference-only intent is not. Hold affected
implementation while comparing it with the active acceptance boundary; do not
demote it to proposal/reference merely because state has not been updated yet.
A SPEC only discovered in the repository may remain non-authoritative, but the
packet may continue only after it is confirmed nonconflicting. Then classify
the result as an amendment, bounded supplement, replacement, or proposal.

- Amendment: update the current active SPEC inside the existing acceptance
  boundary.
- Bounded supplement: the active SPEC links its exact path and scope; this
  active entrypoint controls conflicts and the baseline controls everything
  outside the declared override.
- Replacement: record owner scope/acceptance, name the superseded path or exact
  headings, and switch `active_spec` in the packet transaction.
- Proposal/reference: retain it as non-authoritative input until promoted.

For a non-terminal packet, an owner-requested change inside the same objective
and acceptance boundary may amend or supplement the active SPEC in the same
packet; invalidate and rerun affected evidence. A material objective,
acceptance, protected-boundary, or authorization-term change uses a new packet.
If intent or overlap cannot be determined, ask one blocking question before
affected implementation. A terminal accepted boundary is never reopened.

New additional or replacement SPECs start with this exact metadata block:

```markdown
Status: Proposal | Active | Superseded | Reference
Baseline: SPEC/path.md
Baseline revision: commit/tree/digest | Unpinned proposal
Effective packet: WP-EXAMPLE | Unassigned
Supersedes:
- SPEC/path.md#exact-heading | None (additive)
```

`Effective packet` records the first packet that activates this SPEC revision;
do not rewrite it to follow the current packet. Use `Active` only after exact
incorporation or pointer switch. Existing
single-SPEC projects remain valid without retrofitting metadata. A material
requirement change after owner acceptance uses a new, never-reused packet ID
and new validation; it does not rewrite old acceptance to cover new scope.
Pin the baseline revision when a supplement participates in a terminal packet;
an unpinned proposal remains non-authoritative.

`Active` on a supplement means the state-declared entrypoint incorporated it;
it does not create a second normative entrypoint. Normative supplements must be
readable repository-local paths. Keep lineage acyclic: a SPEC cannot supersede
itself, and overlapping supplements require explicit precedence in the active
entrypoint before implementation. External documents remain references until
their accepted requirements are incorporated repository-locally.

Do not split a SPEC merely because work continues after `COMPLETE`. Split when
targeted reads are no longer practical, independent domains need different
packets, or parallel edits repeatedly conflict. Keep `active_spec` as the short
normative entrypoint and link bounded supplements with exact inherited and
overridden scope; do not duplicate shared acceptance across leaf files.

## DirectPipe Baseline And Authority

- This file is the single normative entrypoint selected by `sdad-state.yaml`.
- `docs/PRODUCT_SPEC.md` is a detailed reverse specification of implemented
  behavior. It is evidence/reference input, not independent future-scope authority.
- Current source, tests, reproducible commands, and runtime observations establish
  observed behavior and may expose stale documentation.
- `CLAUDE.md` remains a preserved legacy project guide. Load only the heading
  needed by the current packet and resolve conflicts through this SPEC, state,
  current source/tests, and owner direction.
- Historical plans under `docs/superpowers/` and old release bodies under `dist/`
  remain history/reference until explicitly incorporated.

## Product Definition

DirectPipe is a cross-platform real-time microphone processing host that loads
VST plug-ins, routes processed audio to normal outputs and the DirectPipe
Receiver, and supports fast control through presets, hotkeys, MIDI, HTTP,
WebSocket, and Stream Deck integrations.

## Origin / Pain

Users need low-overhead, always-available microphone processing without running
a full DAW, while retaining safe live-broadcast control, independent monitoring,
recording, and clean audio delivery to applications such as OBS.

## Owner Control Model

The owner controls product direction, compatibility breaks, release acceptance,
public claims, destructive changes, security/risk acceptance, and whether manual
or hardware evidence is sufficient. A request authorizes only its named packet
and protected action.

## Principles

- The owner controls direction and final acceptance.
- AI output is not completion evidence by itself.
- The state-declared active SPEC entrypoint drives implementation.
- Tests, docs, and reproducible commands prove behavior.
- Future ideas stay out of active work until promoted.
- Current active SPEC sections override older historical sections.
- Obvious but consequential rules must be written down.
- Fuzzy plans should be checked against repository evidence before owner
  clarification.
- Partial, degraded, skipped, or unverified behavior must be labeled.

## Current Architecture

Microphone input enters the JUCE audio device callback, passes through the VST
chain and built-in processing, then fans out to recording, shared-memory IPC,
optional monitor output, and the main audio output. Platform abstractions own
device integration; action/control services dispatch UI, hotkey, MIDI, HTTP,
WebSocket, and Stream Deck requests onto safe execution threads.

## Version Lanes

- Windows v4.2.x is the stable release lane.
- macOS remains beta and Linux experimental until corresponding hardware/runtime
  evidence supports stronger claims.
- Stream Deck is a separately packaged compatibility surface that must stay in
  sync with host actions and API/state contracts.
- Bug fixes may enter v4.2.x when compatibility and release evidence remain
  intact; material architecture or contract breaks require a separately accepted
  SPEC amendment and packet.

## Risk Domains

- Real-time callback allocation, blocking, logging, and object lifetime.
- Thread ownership, lock ordering, async callback lifetime, and shutdown races.
- Audio-device startup/recovery, sample-rate and buffer-size transitions.
- VST scanning/loading and plug-in crash containment.
- Shared-memory IPC layout and DirectPipe Receiver compatibility.
- Settings, preset, recording-path, backup, and restore persistence.
- Stream Deck/API action compatibility and state feedback.
- Cross-platform packaging, updater metadata, release assets, and rollback.

## Active Scope

[packet:DP-421-RELEASE-0001] publishes the software-verified implementation from
[`SPEC/DP-421-0001.md`](DP-421-0001.md) under the release requirements in
[`SPEC/DP-421-RELEASE-0001.md`](DP-421-RELEASE-0001.md). The current leaf binds
the exact `main` integration, `v4.2.1` tag, CI artifacts, checksums, and GitHub
publication evidence while preserving v4.2.x public compatibility.

## Non-Goals

- No service or production infrastructure deployment beyond the GitHub source
  and packaged-product release authorized by the current release packet.
- No mandatory control-API authentication, shared-memory ABI break, real-time
  sample-rate conversion, or separate-process VST sandbox in this bug-fix packet.
- No hardware, installed-artifact, third-party plug-in containment,
  cross-platform runtime, or production-readiness claim without separate evidence.

## Risks

- Lock changes can move work onto the audio callback or introduce lock-order cycles.
- Persistence fixes can destroy the state they are intended to protect unless
  staging, rollback, and failure paths are tested.
- Receiver reconnection changes can create use-after-unmap races with the audio callback.
- CI release-order changes can leave tags or draft releases stranded on failure.
- The pre-existing recording patch and SDAD files are owner work and must remain intact.

## Roadmap

1. Integrate [packet:DP-421-0001] with the latest `directpipe/main`.
2. Re-run the metadata, text, patch, and SDAD checks affected by integration.
3. Push `main`, create `v4.2.1`, and dispatch the exact-tag release workflow.
4. Verify CI, artifacts, checksums, and the public GitHub Release.

## Decision Records

Record durable decisions under `SPEC/adr/`. Use ADRs when future agents need to
know why a decision was made, what alternatives were rejected, and what older
SPEC material was superseded.
A decision normally deserves an ADR only when it is hard to reverse, would
surprise a future maintainer without context, and represents a real tradeoff.

## Domain Language

- Main output: the AudioSettings-selected normal output device.
- Monitor output: the optional independent shared-mode monitoring device.
- Receiver: the VST/AU plug-in consuming DirectPipe shared-memory IPC audio.
- Software-verified: required local software checks passed; it does not mean
  hardware-verified, release-candidate, or owner-accepted.

## Completion Criteria

For [packet:DP-421-RELEASE-0001], the acceptance criteria in
[`SPEC/DP-421-RELEASE-0001.md`](DP-421-RELEASE-0001.md) are normative. The
implementation acceptance criteria remain in
[`SPEC/DP-421-0001.md`](DP-421-0001.md). SDAD Doctor validates the control plane
separately from product, CI, package, and publication evidence.

## Release / Production Readiness Gate

A release packet must satisfy the current DirectPipe pre-release workflow,
Critical 0 for the intersecting scope, exact-version source/artifact identity,
CI and package evidence, relevant manual/hardware checks, dependency/security
checks, updater/release-note/version alignment, rollback considerations, and an
explicit owner release decision. SDAD Doctor never substitutes for those gates.
