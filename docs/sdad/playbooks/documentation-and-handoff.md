# Documentation And Handoff Playbook

Status: On demand
Trigger: durable decision, documentation drift, close-loop maintenance, pause,
resume, handoff, or oversized control file

## One Fact, One Authoritative Home

Write each fact once:

- requirement, behavior, non-goal, or acceptance-criteria change -> active SPEC;
- small spec-unstated implementation decision -> implementation notes;
- hard-to-reverse architecture decision -> numbered ADR;
- unresolved work -> TODO or review finding;
- current execution declaration -> `sdad-state.yaml`;
- cross-session recovery pointers and observed results -> current handoff;
- claim/evidence status -> the applicable evidence or claim record.

A handoff links to these authorities. It does not duplicate full SPEC decisions,
implementation notes, ADR rationale, TODOs, findings, or long command/file logs.

An ADR owns rationale and consequences, not normative requirements. If an ADR
changes behavior or acceptance criteria, update the active SPEC in the same coherence
transaction. Evidence and claim files point to one authoritative owner-decision
record instead of copying acceptance fields.

"One owner-decision record" means one authority per decision, not one global
file. It may be a repository approval record, issue/PR decision, signed record,
or the conditional authorization section in the Delivery Readiness Model; all
other surfaces store only its path/URL/ID and last-observed status.
Authorization permits a named future action under conditions and expiry;
acceptance evaluates a delivered result. Keep them as distinct labeled
decisions even when the owner states both together. Without a durable decision
reference, report evidence-ready only and do not persist owner-accepted status.

Before current state leaves a terminal packet, its authoritative decision
record must bind the packet ID, active SPEC path and exact revision,
source/artifact identity, evidence and claim limits, unresolved risk, and final
owner decision. This record preserves accepted history when the same SPEC path
changes later; other surfaces link it rather than duplicating its fields.
If an owner later corrects, limits, or revokes that decision, append a unique
decision record with a `Revises/supersedes` link and move affected current-claim
pointers to it. Keep the prior record immutable as historical authority.
Decision lineage cannot cycle or revise itself. Competing parallel successors
for overlapping claim scope hold the claim until one owner reconciliation
record explicitly supersedes/retires them and updates every current pointer;
time or ID order never chooses authority.

## Routed Document Semantics

`routed_docs` is an eligible current-packet selection set. Current intent chooses
the path, heading, active section, or targeted match actually read. Membership
never means load every listed file in full. Report only routed documents that
were actually read.

## Control File Budget

- Minimal: one changed active state or documentation surface.
- Normal: state plus the affected ledger or authority.
- Heavy: four or more control files or a new durable decision record.

Keep active files short. Move closed TODOs/findings and old evidence to a
Recently Closed section or timestamped archive, then link them. Do not create a
file solely to make the process look complete.

## Active Record Compaction And Closure

Classify before moving. A TODO/finding leaves an active section only with a
resolution kind plus bounded completion evidence, an authoritative owner
resolution/acceptance decision, or a named superseding packet with a reciprocal
active-item link. Deferral stays Future/Deferred and records its reason and
revisit trigger. An unresolved noncurrent finding stays under
`Future / Deferred Findings` with its identity, severity, packet, evidence, and
restore trigger intact; move it back to Active Findings when its packet becomes
current. Moving text to a new section or archive is not resolution.

For implementation notes, retain decisions that still constrain current code,
promote requirements to SPEC and durable rationale to ADR, route unresolved
work to TODO/findings, and replace promoted text with one pointer. If several
current topics remain, keep `implementation-notes.md` as a small route map and
split by topic. Verify inbound links before archive; never make archives part of
default startup context.

Dates and times on archive names are descriptive, not identity, order, or
currentness. Prefer an existing packet/ADR/IMPL/EVID/CLAIM/ART ID in the name,
never overwrite a collision, and resolve duplicate logical IDs before merge.
Existing date-only archive paths remain valid.

For a v3.1 project with mutable owner-acceptance columns in evidence or claim
tables, preserve existing rows as history. Select one durable record for each
decision, copy or link its provenance once, and replace other live decision
fields with pointers as those records are touched. No mass rewrite is required.

## Documentation Update Check

At packet or handoff boundaries, check only affected surfaces:

- behavior/configuration -> README, runtime docs, active SPEC;
- prompt/tool contract -> adapter or skill source, tests, affected guidance;
- data/security/destructive action -> policy, SPEC, findings, owner gate;
- release/migration/version -> release docs, risk playbook, SPEC;
- public/product/hardware/package claim -> evidence and claim records;
- continuity change -> state pointer and the compact handoff.

If no update is needed, name the files checked and why. Do not claim an evidence
checkpoint while current control state is stale.

## Current Handoff

`current_handoff` is the latest resume checkpoint, not live state. Declare it in
`sdad-state.yaml` only when a real recovery document exists. Read it only for
resume or continuity intent, and verify its pointers against current repository
truth.

On a packet switch, the old pointer must be removed or replaced in the same
coherence transaction. A handoff for another packet cannot remain current.

Use `docs/sdad/handoffs/YYYY-MM-DD-HNNNN-topic.md`, where `HNNNN` is the next
zero-padded ID among repository-known handoffs for that same date. Restart at
`H0001` on a new date; never reuse or renumber within a date. The full
date-plus-ID pair is the identity, and only `current_handoff` records currentness.
Existing unnumbered handoffs remain valid. Resolve parallel same-date collisions
before merge by changing the filename, internal ID, and state pointer together.
Include only repository/branch/worktree/HEAD/dirty state, current goal
and next action, authority paths, last observed validation with claim limits,
open constraints/gates, and bounded resume instructions.

## Close-Loop Gate

Before an evidence checkpoint: run or explain routed checks; synchronize state
and changed authorities; inspect unfinished active records and generated
artifacts when relevant; record residual risk; and keep owner acceptance
separate from evidence-ready implementation.
