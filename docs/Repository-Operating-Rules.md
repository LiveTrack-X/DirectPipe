# Repository Operating Rules

Status: Active
Scope: durable repository policy, loaded by route rather than at every startup

## How To Use This Rulebook

The always-loaded contract is the installed tool adapter at its native path.
Current packet state is `sdad-state.yaml`. File selection belongs to
`docs/INDEX.md`. Read this rulebook only when the current packet changes policy,
authority, evidence, risk, or maintenance behavior.

Open the relevant heading, then follow a playbook only when its trigger applies.
Do not read the whole file merely because it exists. Procedures, examples, and
large checklists belong in `docs/sdad/playbooks/`; current facts belong in
state, SPEC, TODO, findings, notes, or evidence files.

## Core 5

1. Current beats historical.
2. Evidence beats confidence.
3. Active beats interesting.
4. Owner decision beats AI momentum.
5. Repeated pain becomes a rule.

Compression first. Gates stay real.

## Source Of Truth

Resolve conflicts by fact type before freshness:

1. the current applicable owner instruction controls requested direction and
   change intent; when it redirects work, reconcile it into the appropriate
   repository authority before stateful implementation resumes;
2. current source code, migrations, tests, runtime state, and reproducible
   commands establish observed behavior;
3. active runtime and operating documents establish current controlled state;
4. `sdad-state.yaml#active_spec` names the single normative entrypoint for
   intended scope, behavior, non-goals, and acceptance criteria;
5. another SPEC applies only where that entrypoint incorporates its exact path
   and bounded scope; the entrypoint controls conflicts in that scope;
6. `sdad-state.yaml` owns the current packet, validation, gates, routes, and
   dominant checkpoint, not cumulative evidence or acceptance history;
7. the state-declared current handoff provides cross-session continuity only;
8. unincorporated SPECs, product notes, and external references are proposals or
   input, not implementation authority;
9. archives and historical records provide rationale; old chat memory and AI
   confidence are hints only.

If a SPEC spans past-to-present history, current active sections override older
background, roadmap, and archived material. This exception resolves chronology
inside the source; it does not let an unapproved draft override owner scope.

Read order is routing, not authority. Current evidence can disprove stale
documentation, but it cannot choose future product scope. Owner decisions
control direction, risk tolerance, irreversible actions, and acceptance. Record
durable owner decisions in the appropriate active source of truth.

`SPEC-COMPLETE.md` is the default integrated baseline, not a frozen artifact.
A SPEC supplied as current requirements is a change request unless the owner
limits it to review/draft/reference. Hold affected work and reconcile it through incorporation or
an `active_spec` switch transaction. A merely discovered file gains no authority
from its filename, date, sequence, status label, or provider session. Continue
only after confirming it is non-authoritative and nonconflicting. An ADR records
rationale and cannot override normative SPEC content alone.

A handoff-only decision is continuity until routed to one authoritative home.
Before implementation, put scope, behavior, and acceptance-criteria changes in
the active SPEC; durable architectural rationale in an ADR; claim/evidence
status in its ledger; unresolved work in TODO/findings; and owner authorization
or result acceptance in one durable owner-decision record. Link it elsewhere
instead of choosing several homes.

## Owner Authority And Evidence States

Keep these states distinct:

- planned: the packet has not produced evidence;
- evidence-ready: scoped implementation and checks are shown;
- software-verified: required local software evidence passed;
- tester-ready or hardware-verified: the corresponding external evidence exists;
- release-candidate or production-ready: every named release gate passed;
- owner-accepted: the owner or delegated policy accepted the result.

Owner acceptance cannot upgrade weak evidence. A passing evaluator cannot
approve scope. A successful commit cannot authorize push, release, deploy,
migration, destructive action, or risk acceptance.

Work may continue inside an approved packet. Obey a current owner stop/redirect.
Pause for input when scope expansion is unrequested or ambiguous, a protected
action/claim gate changes, an irreversible action remains unauthorized, an
owner-controlled tradeoff is unresolved, verification is blocked, or evidence
conflicts with the requested plan.

## Non-Negotiable Boundaries

### Sensitive Data

Sensitive data is an authorization boundary, not a size threshold. Use
metadata-only inspection by default. Do not read, copy, transmit, summarize, or
paste credentials, private keys, tokens, cookies, `.env` contents, raw
customer records, or private corpora unless the task requires it and owner policy plus tool policy explicitly permit it.

Prefer redacted samples, schemas, counts, filenames, and hashes. Keep sensitive
content out of prompts, logs, TODOs, findings, handoffs, and generated
artifacts. If authorization is unclear, stop before reading.

### Irreversible And External Actions

Release, deploy, migration, destructive action, real-user-data handling, auth,
money, security policy, rollback, public claims, and external messages require
the owner gate named by the active packet. Reversible local implementation does
not imply authority for the external action.

### Context Stability

Check size and relevance before reading large, generated, historical, or broad
inputs. Use bounded reads above 50 KB or 500 lines. Do not read files above
1 MB in full during startup unless historical reconstruction is explicitly
requested. Use the context-and-data playbook for private or oversized inputs.

## Code Consistency

Current behavior and acceptance criteria must agree. Before implementation:

- inspect current source and tests rather than trusting a plan summary;
- identify the active SPEC slice, non-goals, packet boundary, and evidence
  expectation;
- prefer the simplest design that satisfies the current contract;
- keep changes surgical and separate active implementation from future ideas;
- add a failing test/check before behavior changes when the surface is testable;
- label partial, degraded, scaffolded, simulated, skipped, and unverified work.

A green test suite proves only the claims covered by those tests. When package,
browser, persisted-state, hardware, remote, or production behavior is claimed,
run evidence at the corresponding tier.

## Durable Decision Policy

Write each decision once:

- active SPEC: scope, behavior, non-goal, acceptance criterion;
- implementation notes: a spec-unstated implementation judgment and its
  verification impact;
- ADR: a hard-to-reverse, surprising, durable tradeoff;
- TODO: current or deferred work;
- review findings: defect, failed check, unresolved risk, or blocked gate;
- claim/evidence files: allowed, qualified, or blocked claim status;
- `sdad-state.yaml`: current execution declaration;
- handoff: cross-session recovery pointers/results only.

Do not store raw internal reasoning, mechanical edit journals, or copied logs in
active controls. For repeated pain or one high-risk control failure, record the
finding/root cause, choose the smallest durable control plus regression evidence,
then Keep/Refine/Merge/Retire it after use. Prefer refining an existing rule,
flow, check, or template before adding a new global rule.

## Review And Verification

A packet is evidence-ready only when:

1. the approved behavior or documentation change is implemented;
2. relevant tests and reproducible checks pass, or the exact block is recorded;
3. the evidence tier matches every claim;
4. changed control state, SPEC, TODO, findings, notes, and docs are synchronized;
5. unfinished packets, generated artifacts, caches, and package outputs are
   checked when relevant;
6. remaining risk, unverified behavior, owner decisions, and acceptance state are
   explicit.

Review findings gate intersecting packet, artifact, safety, or claim work unless
the owner accepts that bounded risk. They do not choose the next objective.
Unrelated findings may be deferred with packet links and revisit triggers; they
do not globally veto owner-selected work.
Reference-derived work requires a source-to-implementation-to-evidence parity
map. Package or release work requires smoke testing outside the source tree
when the installed artifact is part of the claim.

## DirectPipe Project Constraints

- Treat current source, tests, and runtime evidence as observed behavior. Use
  `CLAUDE.md` only by the relevant heading for detailed thread, lock,
  maintenance, and pre-release rules that have not moved to shared docs.
- Real-time audio callbacks must not allocate, block on ordinary mutexes, log,
  or perform filesystem/network work. Preserve declared thread ownership and
  lock order when changing audio, device, plugin, IPC, or control paths.
- Use `TESTING.md` and `.github/workflows/build.yml` for current build/test
  commands. On this Windows machine, CMake is not on PATH; use the documented
  Visual Studio CMake path rather than assuming a global command.
- A code change must check the affected module README and user/architecture/API
  documentation. Documentation drift is an unresolved defect, not completion.
- IPC layout/protocol, Receiver plug-in behavior, VST format support, Stream
  Deck actions, settings/preset persistence, and release assets are compatibility
  boundaries. Match claims to focused tests and real-device/artifact evidence.
- Before push, tag, or GitHub Release, verify the remote explicitly. The
  DirectPipe release remote is `directpipe`; do not infer that `origin` is safe.

## On-Demand Playbooks

Load only the triggered playbook:

- `sdad/playbooks/context-and-data.md`: large/private input and context recovery;
- `sdad/playbooks/work-packets.md`: scale, execution scope, packet, delegation;
- `sdad/playbooks/evidence-and-risk-gates.md`: owner gates, claims, parity, release;
- `sdad/playbooks/documentation-and-handoff.md`: record routing, budgets,
  maintenance, state, and handoff;
- `sdad/playbooks/advanced-extensions.md`: harness, eval, memory, and loop fit.

## Rule Maintenance

Keep this file policy-only and reasonably short. Move procedures and examples
to playbooks. Keep INDEX routing-only and active state current. Archive closed
history. If a rule is mechanically enforceable, prefer a test or validator over
duplicated prose. If no current packet needs a rule or playbook, do not load it.
