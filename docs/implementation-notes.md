# Implementation Notes

Status: Active
Scope: Current spec-unstated implementation decisions

Use this file when implementation requires a judgment the active SPEC did not
explicitly make.

Do not use this as a transcript of raw internal reasoning or a mechanical edit
log. Keep it short enough for a fresh AI session to read as current context.

## Current Notes

## IMPL-0002 - Integrate Audit Fixes In One Compatibility-Bounded Packet

- Date: 2026-07-15
- Applies to: [packet:DP-421-0001].
- SPEC gap: The audit findings span independent subsystems, while the owner asked
  for all findings to be improved as one v4.2.1 update.
- Decision: Use one active packet with parallel review-worthy units in the shared
  worktree, preserve the pre-existing recording patch, and integrate before any
  completion claim. Compatible mitigations stay in packet; ABI/API-breaking or
  separate-process architecture changes remain explicit residual findings.
- Why: The fixes share one release-lane acceptance boundary and one aggregate
  validation surface, but can be implemented in non-overlapping source areas.
- Alternatives rejected: Reopen the terminal SDAD bootstrap packet; silently
  reduce the owner's requested scope; treat parallel unit success as aggregate completion.
- Supersedes: The deferred `DP-REC-0001` placeholder, whose recording work is now
  incorporated into `DP-421-0001`.
- Verification impact: Each unit needs focused evidence, followed by integrated
  Release build/CTest and SDAD structural validation.
- Outcome: Software verified on 2026-07-15 with the integrated Windows Release
  build, 484 registered tests (0 failed, 2 skipped), Stream Deck 5/5 plus
  build/validation, metadata/text/patch gates, and SDAD v3.2.2 strict Doctor.
- Follow-up: Resolved by [packet:DP-421-RELEASE-0001] and owner decision
  `OD-2026-07-27-0001`.

## IMPL-0001 - Preserve Existing DirectPipe Authorities During SDAD Bootstrap

- Date: 2026-07-15
- Applies to: [packet:DP-SDAD-0001] and the repository control plane.
- SPEC gap: DirectPipe had a large implementation-derived product specification
  and a Claude-specific project guide, but no shared packet/state router.
- Decision: Keep existing source and documents in place, make
  `SPEC/SPEC-COMPLETE.md` the single normative entrypoint, classify
  `docs/PRODUCT_SPEC.md` as an implementation-derived reference, and route
  `CLAUDE.md` only by targeted heading until shared rules absorb its content.
- Why: This adds durable cross-agent control without rewriting provenance or
  mixing the owner's in-progress recording patch into the migration.
- Alternatives rejected: Overwrite `CLAUDE.md`; treat the reverse spec as future
  requirement authority; copy all historical plans into the startup route.
- Supersedes: None.
- Verification impact: Doctor proves structural coherence only. DirectPipe
  product checks remain separate and packet-specific.
- Follow-up: Reconcile stale legacy-guide facts during the next documentation or
  release packet, and replace state validation for each new implementation packet.

## Routing

- If the note creates future work, update `docs/TODO-Open-Items.md`.
- If the note records a bug, risk, or blocked issue, update `review-findings.md`.
- If the note is durable architecture, policy, release, security,
  data-boundary, or owner-approved tradeoff rationale, create or update an ADR.
  A decision normally deserves an ADR only when it is hard to reverse, would
  surprise a future maintainer without context, and represents a real tradeoff.
- New durable notes use a never-reused `IMPL-NNNN` ID. Existing unnumbered notes
  remain valid. Date is descriptive; identity and supersession use the note ID.
- At packet boundaries, classify each note by current effect rather than age:
  keep current small constraints here; promote requirements to SPEC, durable
  rationale to ADR, work to TODO, and defects/risks to findings.
- When a note is promoted or superseded, leave only a pointer to the new
  authority. Do not keep two mutable copies of the same decision.
- If current notes exceed a bounded read or mix unrelated domains, split by
  topic and keep this file as the small current router. Archive only decisions
  that no longer affect current work, verify inbound links, and never route all
  archive files at startup.
