# ADR-0001: Decision Title

Status: Proposed
Date: YYYY-MM-DD
Owner: Name or role
Scope: Project, subsystem, release, or SPEC slice

Use an ADR when the decision is hard to reverse, would surprise a future
maintainer without context, and represents a real tradeoff. Smaller
spec-unstated implementation choices belong in `docs/implementation-notes.md`.

## Context

Describe the problem, constraints, current active SPEC path, and why a durable
decision record is needed.

## Decision

State the decision clearly.

## Rationale

Explain why this option was chosen.

## Alternatives Considered

- Option A:
- Option B:
- Option C:

## Consequences

Positive:

- TBD

Negative or tradeoffs:

- TBD

## Evidence

List evidence used to make the decision:

- tests or commands,
- prototypes,
- review findings,
- owner constraints,
- external references,
- production or release requirements.

## Current-Over-Historical Rule

An ADR owns rationale, alternatives, tradeoffs, and consequences. It does not
override normative scope, behavior, or acceptance criteria in the active SPEC by itself.
A proposed ADR is never implementation authority. When an accepted ADR changes
normative behavior, update the active SPEC and ADR in the same coherence
transaction; until then, the active SPEC controls.

Use `Supersedes` primarily for earlier ADRs or decision records. List affected
SPEC sections separately and link the accepted ADR from the active SPEC.

Affected SPEC sections:

- None

Supersedes:

- None

Superseded by:

- None

## Follow-Up Work

- [ ] Update active SPEC if needed.
- [ ] Update docs or runbook if needed.
- [ ] Add tests or review checks if needed.
- [ ] Update TODO or review findings if needed.
