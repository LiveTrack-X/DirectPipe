# Advanced Extensions Playbook

Status: On demand
Trigger: repeated eval loop, adaptive-rule portability, harness optimization,
self-improving agent loop, retrieval/memory tuning, scheduled loop, or
model-routing experiment

Do not add an advanced control surface because it is interesting. Require:

- a representative task and environment plus a measurable baseline;
- deterministic outcome checks where possible and a fixed model/tool setup;
- separate regression and capability evaluation;
- held-out or fresh tasks for public comparative claims;
- repeated runs for nondeterministic workflows;
- human-calibrated semantic graders when deterministic checks are insufficient;
- final-answer completeness in addition to tool-trace correctness;
- separate evidence-ready and owner acceptance criteria;
- evaluation leakage and private-data controls;
- rollback and stop rules plus an owner adoption gate.

Apply a quality-first budget: meet the quality and evidence bar before comparing
time, tokens, latency, retries, review burden, compute, or cost. Record those
budgets, the rollback trigger, and the owner-controlled stop condition.

Mark missing fields as unknown or blocking. Keep advisor approval, worker
completion, evaluator pass, and owner acceptance as separate states. A passing
loop evaluator cannot approve a new scope, release, or destructive action.

Prefer isolated exploration for one-off research, a skill/playbook for repeated
procedure, deterministic validation for required guarantees, and reviewed
project memory for durable decisions. Remove the extension when its maintenance
cost exceeds the measured benefit.

## Adaptive Rule Portability

Use this only when a Rule 5 control must be inspected across projects. Keep a
single project-specific rule in its existing human-readable authority by
default. Create an adaptive-rule index only after multiple promoted rules need
independent lifecycle, review, or portability; never create it as bootstrap
ceremony or hide the canonical rule in agent memory or an opaque database.

An exported rule proposal is readable Markdown and records stable ID/revision,
status, origin/lineage, trigger and non-trigger, observed failure, root cause,
operational rule, exceptions, enforcement, regression evidence, limits, owner
decision, and Keep/Refine/Merge/Retire condition. A directory pack may add a
human-readable `RULEPACK.md`, rule files, and checksums, but it contains no
automatically executed code and no secrets, raw private data, private chat,
absolute local paths, or unportable evidence.

Import authority follows the current owner's requested action, not external
origin:

- explicit apply/adopt/implement -> owner-directed integration; do not ask for
  duplicate approval, but adapt local enforcement and validate before Active;
- review/compare/reference -> non-authoritative review input;
- automatic or agent-discovered input -> candidate requiring an owner decision.

An owner-directed rule is not silently downgraded because it was imported. If it
conflicts with Core authority, a protected gate, or another current owner
instruction, preserve the apply decision, integrate nonconflicting parts, and
hold only the conflict for a concrete owner decision. Never auto-modify Core
rules, active SPEC/state, gates, or executable validators from a pack. Preserve
lineage for round trips and show a human-readable diff; never auto-merge.

This playbook defines a manual, reviewable portability protocol. It does not
require a plugin, remote rule loader, background synchronization, or new CLI.
Add automation only after repeated real use proves that the manual flow is the
costly or error-prone part and the automation has deterministic safety tests.
