# Work Packets Playbook

Status: On demand
Trigger: scale, execution-scope, packet, delegation, planning, or implementation
decision

## One Work Loop

Use one loop: Plan -> Route -> Implement -> Verify -> Report. An owner gate or
handoff is a triggered branch, not another loop. Keep iteration bounded inside
the selected unit or packet.

## Scale And Execution Scope

Use the smallest persistent control surface that protects the work:

- One-shot: the current request only; no persistent state is implied.
- Mini: one bounded unit.
- Standard: one executable packet.
- Full: one executable packet plus every applicable owner gate.

Standard is suitable for work that inspects, documents, or tests a protected
area without taking the protected action. Full is suitable when the packet
changes, accepts, or executes a release, production, migration, destructive, data, auth,
money, security, rollback, or equivalent owner-controlled boundary.

State v2 execution scope is `unit` or `packet`. It controls the current work
boundary, not risk acceptance. Multi-packet execution requires an explicitly
approved packet plan or list. Infer scale and gates from the request and
repository first.
Ask at most one blocking question only when the answer changes objective/direction,
authority/reference role, execution boundary, protected action/gate, or claim boundary.
An explicit current owner command authorizes only its named direction/action and
stated boundary; persist it and do not ask for the same decision again. It does
not waive evidence, prerequisites, tool policy, or another protected action.
Classify the whole utterance: questions, hypotheticals, quotations, negations,
and review/reference-only requests do not authorize their mentioned actions.

## Packet And Delegation Envelope

Every packet or delegated assignment records:

- the desired outcome or objective and its acceptance boundary;
- the exact state-declared active SPEC, baseline, authority, and reference paths;
- constraints, allowed scope and non-goals;
- validation commands and the claims each check supports;
- expected evidence, required evidence, and claim limits;
- owner gates and stop conditions;
- the required report, including residual risk and unverified work.

A review-worthy unit is an internal slice large enough to review and small
enough to verify. Delegated workers receive the full envelope because they may
not inherit parent context. Continue inside the declared boundary until an
evidence checkpoint unless a stop condition fires.

## Packet Split Decision

Split implementation when any boundary becomes independently reviewable:

- acceptance or validation contract;
- owner gate or version/release lane;
- rollback, blocker, retry, or cost budget;
- subsystem, reviewer, environment, or evidence source.

Line count alone is not a split rule. Never shrink an original packet's
acceptance boundary after work starts merely to make a completed subset green.
If independent unit boundaries were declared before execution, each may become
its own packet. If a split is discovered later, keep the original unaccepted,
record one durable split decision, then switch to one child. The record pins:

- parent packet ID, objective, active SPEC path and revision;
- original acceptance criteria and non-goals;
- validation commands, proves claims, and required aggregate checks;
- owner gates plus authorization references and conditions;
- carried evidence and its freshness limits;
- child packet IDs, split reason, reciprocal TODO/finding links, and resume trigger.

Use one implementation-note decision by default or an ADR only for a
hard-to-reverse architecture split; other records link it. While a child is
current, keep the parent and inactive siblings under `Future / Deferred` with
their original IDs, split-decision links, and resume triggers; only the current
leaf belongs in Active Work. Move their unresolved findings intact to
`Future / Deferred Findings`, preserving severity, packet marker, evidence, and
revisit trigger; only current-leaf findings stay active. If the units remain inseparable, keep the packet
blocked and record partial evidence. State declares one current leaf; discovery
never expands the approved future-packet list.
After the children finish, reconcile their outcomes against the original
boundary. Close the original only after aggregate validation and an owner
decision, or record an explicitly accepted replacement that retires it; an
individually green child cannot close the parent by itself. Reselect the
non-terminal parent for aggregation or declare one integration packet; failed,
cancelled, or incomplete children remain reciprocal unresolved records. When
the parent becomes current again, reconstruct state from the split-decision
envelope, review SPEC/source/evidence/authorization freshness, move only its
record back to Active Work, restore its deferred findings to Active Findings,
then run aggregate validation before an owner decision.

## SPEC Lineage Transaction

`active_spec` names the single normative entrypoint for the current leaf packet.
A filename, `FINAL`/`COMPLETE` label, date, sequence number, or `Status: Active`
line does not grant authority by itself. A SPEC supplied as current requirements
is a change request unless the owner limits it to review/draft/reference; do not
demote it merely because the state pointer still names the old SPEC. A SPEC only
discovered in the repository is not automatically authoritative. A
review/compare/explain request stays read-only without incorporating it.

Before affected implementation continues, hold affected work and classify owner
intent and overlap with the active objective, acceptance, protected boundary,
and authorization terms:

- owner-requested change inside the same unfinished acceptance boundary ->
  incorporate it as an amendment or bounded supplement, keep the packet, and
  invalidate affected evidence;
- owner-requested material change, or any change to a terminal accepted
  boundary -> create a new packet and perform the switch transaction;
- explicitly draft/reference or unrequested, nonconflicting input -> retain it
  as proposal/reference and continue the current packet;
- conflicting or possibly authoritative input whose intent/overlap is unknown
  -> hold affected implementation and ask one blocking question if repository
  evidence cannot resolve it.

For an accepted change, record its lineage before implementation:

- amendment inside the existing acceptance boundary -> update the active SPEC;
- bounded supplement -> record baseline, effective packet, and exact overridden
  paths/headings; the active entrypoint controls conflicts in that scope;
- full replacement -> record the superseded SPEC and switch `active_spec` in
  the same coherence transaction;
- draft/reference -> retain as non-authoritative input.

Normative supplements must be readable repository-local paths. Reject
self-supersession and lineage cycles. When supplements overlap, the active
entrypoint must name exact precedence before implementation; filename order or
the newest edit cannot resolve the conflict.

If scope, acceptance, a protected boundary, or authorization action/conditions
materially change, stop and create a new, never-reused packet ID. Satisfying an
already-declared gate does not itself change the packet. An owner-accepted or
production-ready packet is immutable history: later requirements use a new
packet rather than rewriting its accepted boundary. On activation, recheck every
pre-approved queued packet against the active SPEC before it becomes the leaf.

## Packet Switch Transaction

Treat a packet switch as one coherence transaction:

1. Select next leaf packet and outcome without changing live state.
2. If the old packet is terminal, first confirm one durable decision record
   binds its packet ID, active SPEC path and exact revision, source/artifact
   identity, evidence and claim limits, unresolved risk, and final owner
   decision. Link an existing project decision surface or the Delivery
   Readiness fallback; do not reconstruct this boundary from a mutable path.
3. Classify old active records: move closed history, defer future work, and
   relink deliberately carried work to the selected packet. Closure requires
   evidence, an authoritative owner decision, or a named superseding packet.
4. Review validation entries; remove stale checks, revise claim text, and add
   the smallest task-specific check.
5. Update state as one bundle: date, `active_spec`, packet identity/objective/status,
   `validation_for`, gates, validation, and eligible routes.
6. Remove or replace the old `current_handoff`; never carry it across
   mechanically.
7. Run Doctor strict on the coherent declaration and active ledgers.
8. Run project checks separately and record their bounded evidence.
9. Advance status only after the required evidence exists.
10. Rerun Doctor because status-sensitive severity may have changed.

## Long-Running Re-entry And Invalidation

State v2 `active_packet.status` is the current dominant checkpoint, not a
cumulative evidence or acceptance ledger. Preserve prior evidence and owner
decisions in their authoritative records when status moves backward or forward.
Apply the first matching rule below. Keeping a packet always requires the same
unfinished objective, acceptance, protected boundary, and authorization terms.
Never move a terminal `owner_accepted`/`production_ready` packet backward: a change
to its accepted SPEC, source, or artifact inside the accepted claim boundary
starts a new packet, while unrelated repository edits do not. A late
observation of the unchanged result stays historical evidence or uses a named
revalidation/occurrence packet.

| Re-entry event | Required action |
| --- | --- |
| Owner changes, narrows, or replaces the current direction | Stop affected local and delegated work; classify the instruction as additive or replacing, re-plan/re-route, and treat old-boundary outputs as stale until reconciled and revalidated. |
| Owner cancels the current packet without a replacement | Stop affected work; set the current packet to `deferred`, preserve partial evidence and the cancellation reason in a packet-linked record, and set the resume trigger to explicit owner reactivation. Never auto-resume it or create a handoff unless actual continuity is required. |
| Same unfinished objective and unchanged acceptance/gates | Continue the same packet; invalidate and rerun only affected evidence. |
| Owner directs adoption/implementation of a new or conflicting SPEC | Treat it as a current change request; hold affected work, then amend the same non-terminal packet or switch to a new packet according to the acceptance boundary. |
| Owner asks to review/compare/explain a SPEC | Keep the request read-only; report conflicts without incorporating, switching packets, or implementing. |
| Unrequested additional SPEC is discovered | It gains no authority from filename, date, or status; continue only after confirming it is non-authoritative and nonconflicting, otherwise hold and reconcile. |
| Material objective, acceptance, protected boundary, or authorization-term change | Create a new unique packet and run the switch transaction. |
| Existing declared gate becomes authorized/satisfied | Continue the same packet when action, conditions, source/artifact identity, and expiry still match. |
| Non-terminal source, SPEC, validation, generator input, artifact, or environment changes after evidence | Hold the claim, move status back to the applicable checkpoint, and revalidate the affected surface. |
| Terminal packet's accepted SPEC, source, or artifact changes inside its accepted claim boundary | Preserve revision-bound history and create a new implementation/revalidation packet. |
| Defect or contradictory evidence appears after acceptance | Link a finding to the revision-bound decision and open a new bugfix/revalidation packet; do not rewrite the accepted record. |
| Merge, rebase, cherry-pick, or conflict resolution | Reconcile SPEC/state/ledger/ID changes; never choose incoming state wholesale; validate the final integrated worktree/tree, and bind release evidence to the exact commit/HEAD and artifact. |
| Crash, missing handoff, or stale provider session | Re-enter through adapter -> state -> INDEX and reconstruct from repository truth. |
| Result from a background job launched by the same unfinished packet | Return through Plan/Route; keep the packet only if identity/acceptance/gates match, and never upgrade status retroactively. |
| Late external/hardware result for terminal or changed scope | Preserve history or use a named revalidation packet; never rewrite accepted scope. |
| Repeated/flaky check | Record attempts, skips, instability, and a bounded retry budget; do not select one green run as proof. |
| New recurring maintenance occurrence | Use a new occurrence packet and evidence identity; the prior run does not authorize or validate the next. |
| Blocked or deferred packet | Record the blocker/deferral, partial evidence, and explicit resume trigger in a packet-linked TODO, finding, or owner gate; only that trigger can reactivate it, and unrelated work first switches packets. |
| Owner requests changes after Report | Keep the packet only when it is non-terminal and acceptance/protected boundary/authorization terms are unchanged; otherwise allocate a new packet. |
| Owner revises or revokes a terminal decision | Preserve the old decision; create a new decision/revalidation packet and unique decision record that revises/supersedes it, then update affected current-claim pointers. |
| Validation command or test surface is weakened | Reassess the `proves` claim; a greener but weaker check cannot preserve the prior evidence tier. |
| Version or release lane changes | Use the exact lane SPEC and a new packet unless the existing acceptance explicitly covers both lanes. |

Parallel independent write branches/worktrees use distinct child leaf packet IDs;
read-only reviewers may reference the same packet without claiming a write
leaf. Before merge, compare base/HEAD, active SPEC, objective, authorizations,
record IDs, decision predecessor edges, current-claim pointers, and handoffs;
resolve collisions/forks and rerun integration/release checks on the final
integrated worktree/tree. Bind release evidence to the exact commit/HEAD and
artifact. An approved multi-packet list never expands when a queue discovers
work outside that list.

## Implementation Memory

Record a small spec-unstated implementation decision in
`docs/implementation-notes.md`. Use an ADR only for a hard-to-reverse,
surprising architectural tradeoff. Put unresolved work in TODO or findings,
not in the handoff. Split implementation notes by current effect and topic, not
age; promote or supersede a note before archiving it.

## Implement And Verify

Read-only review or planning may mark Implement N/A; a blocked packet may mark
Verify blocked. Report every omission and never claim evidence the skipped
phase would have produced.

### Bounded Iteration

For non-trivial work, use `inspect -> act -> observe -> update -> retry or stop`.
Set bounded attempts and do not repeat a failed action without new evidence, a
changed hypothesis, or an explicit remaining retry budget. Keep this iteration
inside the Implement and Verify phases of the one work loop.

## Stop Conditions

Obey an explicit stop or redirect. Ask only when an unresolved fact changes
objective/direction, authority/reference role, execution boundary, protected
action/gate, or claim boundary; also stop for blocked verification or conflicting
evidence. Do not treat a clear owner expansion as ambiguity or silently cross an
unrequested boundary.
