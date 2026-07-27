# DirectPipe Documentation Router

Status: Active

## First Read

1. Read `../sdad-state.yaml` for the active packet, gates, validation, and eligible routes.
2. Read this router.
3. Inspect current source, tests, and runtime evidence.
4. Read only the path, heading, section, or targeted match selected by intent.

Do not load full rules, large specs, history, or evidence ledgers by default.

## Working Route

| Intent or trigger | Read now | Load on demand |
| --- | --- | --- |
| Any active packet | state, source/tests | one intent-selected route |
| Implement or fix | active SPEC, active TODO/findings | implementation notes; ADR |
| Audio, device, recording | current source/tests | `../CLAUDE.md` thread/lock headings; `ARCHITECTURE.md`; targeted `PRODUCT_SPEC.md` |
| IPC, Receiver, VST | core/plugin source and tests | `ARCHITECTURE.md`; targeted `PRODUCT_SPEC.md` |
| Stream Deck/control API | control/plugin source and tests | `STREAMDECK_GUIDE.md`; `CONTROL_API.md` |
| Build or test | `../TESTING.md`, CI | `BUILDING.md`; `../CLAUDE.md` Build heading |
| New/conflicting SPEC | owner request, active SPEC, source/tests | supplied SPEC; work-packets playbook |
| Review or docs | source/tests, SPEC, active findings | affected docs; operating-rule heading |
| Release, tag, push, package | gates, findings, changelog, CI | current release body; pre-release/risk playbook |
| Product/hardware/public claim | claim/evidence files when created | artifact, readiness, remote evidence |
| Pause/resume/handoff | state and declared current handoff | work/evidence playbook |
| History/reference | current authorities first | `docs/superpowers/`, old `dist/` bodies |

## On-Demand Policy And Playbooks

- Policy/authority: `Repository-Operating-Rules.md` by heading.
- Large/private input: `sdad/playbooks/context-and-data.md`.
- Scope/packet/delegation: `sdad/playbooks/work-packets.md`.
- Gates/claims/release: `sdad/playbooks/evidence-and-risk-gates.md`.
- Docs/state/handoff: `sdad/playbooks/documentation-and-handoff.md`.
- Harness/eval/memory fit: `sdad/playbooks/advanced-extensions.md`.

## Write Route

| New information | Record in |
| --- | --- |
| Scope, behavior, acceptance | active SPEC |
| Owner authorization or acceptance | `owner-decisions.md` |
| Current/deferred work | `TODO-Open-Items.md` |
| Defect, failed check, risk | `../review-findings.md` |
| Spec-unstated choice | `implementation-notes.md` |
| Durable tradeoff | numbered ADR under `../SPEC/adr/` |
| Evidence/claim status | on-demand evidence/claim ledger |
| Execution or continuity | state; declared handoff only when needed |

## Source Of Truth

Current owner direction controls requested change intent. Source/tests/runtime
establish observed behavior. The state-declared active SPEC is the single
normative entrypoint. State owns execution; handoff owns continuity. Other
SPECs, historical plans, legacy guides, and chat memory do not activate scope.

## Active Catalog

- Core: state, `../AGENTS.md`, active SPEC, owner decisions, TODO, findings,
  implementation notes.
- Policy: `Repository-Operating-Rules.md`; procedures: `sdad/playbooks/`.
- Legacy guide: `../CLAUDE.md` by targeted heading only.
- Optional evidence/claim files: create only for an active claim.
- Current handoff: use `../sdad-state.yaml#current_handoff` when declared.
- Decisions: `../SPEC/adr/`.

## Maintenance

Keep this routing-only and within the startup budget. Archive closed history;
report documents actually read and checks actually run.
