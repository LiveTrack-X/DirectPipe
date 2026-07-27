# DirectPipe v4.2.1 Release

Status: Owner accepted
Baseline: SPEC/SPEC-COMPLETE.md
Source packet: DP-421-0001
Effective packet: DP-421-RELEASE-0001
Owner decision: docs/owner-decisions.md#OD-2026-07-27-0001

## Objective

Integrate the software-verified v4.2.1 reliability update into the current
`LiveTrack-X/DirectPipe` `main`, tag the exact release source as `v4.2.1`, and
publish the GitHub Release only after the repository `Build & Release` workflow
has built and validated every advertised artifact.

## Authorized Scope

- Commit the remaining DP-421-0001 device-recovery changes.
- Merge the latest `directpipe/main` without discarding the current public
  README/DPWIM information or any v4.2.1 source, tests, and release metadata.
- Push the integrated tree to `LiveTrack-X/DirectPipe` `main`.
- Create and push the annotated tag `v4.2.1`. If exact-tag CI fails before any
  GitHub Release exists, record the failed source identity, fix the release
  blocker, delete the unpublished tag, and recreate it once on the corrected
  source. A published release tag is immutable.
- Dispatch `.github/workflows/build.yml` with `release_tag=v4.2.1`.
- Allow that workflow to create the public GitHub Release from
  `dist/release_body.md` only after all Windows, macOS, Linux, Stream Deck, and
  checksum artifacts exist.
- Record the final commit, tag, CI run, assets, checksums, and publication URL.

## Acceptance Criteria

1. The integrated `main` contains DP-421-0001 and the latest pre-existing
   `directpipe/main` changes without unresolved conflicts or whitespace errors.
2. Canonical version metadata and the current/versioned release bodies agree on
   `4.2.1`; repository text integrity and SDAD Doctor strict remain green.
3. The deferred-finding scan reports Critical 0 for this release scope. Medium
   architecture findings remain outside the v4.2.x compatibility boundary.
4. `v4.2.1` points to the exact integrated release-source commit and is pushed
   without rewriting another tag.
5. The `Build & Release` workflow succeeds for that tag and publishes exactly
   these assets:
   - `DirectPipe-v4.2.1-Windows.zip`
   - `DirectPipe-v4.2.1-macOS.dmg`
   - `DirectPipe-v4.2.1-Linux.tar.gz`
   - `com.directpipe.directpipe.streamDeckPlugin`
   - `checksums.sha256`
6. `checksums.sha256` names and hashes all four downloadable product artifacts.
7. The GitHub Release is public, non-prerelease, latest, and uses the checked-in
   v4.2.1 release notes.

## Evidence And Claim Limits

- Carried local evidence from DP-421-0001: Windows Release app/Receiver/tests
  built; 488 registered tests completed with 0 failures and 2 declared
  environment-dependent skips; Stream Deck tests passed 5/5 with build and
  package validation.
- The final merge invalidates only affected metadata/documentation evidence when
  it does not alter tested source. Re-run version, text, patch, and SDAD checks
  on the integrated tree. The exact-tag CI build supplies final package evidence.
- No real-device, Windows Voice Clarity, installed-artifact smoke, third-party
  VST crash-containment, macOS hardware/runtime, or Linux hardware/runtime claim
  is made. The owner explicitly authorized release with those limits.

## Owner Gates And Stop Conditions

- Owner authorization is satisfied by `OD-2026-07-27-0001`.
- Stop if the latest `directpipe/main` introduces a source/contract conflict,
  a Critical finding intersects the release, the tag already exists, the exact
  release-source checks fail, or any CI build/package/publish job fails.
- Do not force-push, move a published release tag, publish partial assets,
  weaken the IPC/API/Stream Deck compatibility contract, or claim hardware
  verification.

## Rollback

- Before publication, a failed run leaves no public release. A stale unpublished
  draft may be deleted and recreated by the workflow. An unpublished tag may be
  recreated only after recording the failed source and confirming that no
  GitHub Release exists.
- After publication, never rewrite `v4.2.1`. Withdraw the release if necessary
  and publish a new corrective version from a new packet and tag.

## Validation Contract

- `bash tools/pre-release-test.sh --version-only`
- `python tools/check_text_integrity.py --repo-root .`
- `git diff --check`
- `python .../sdad.py doctor . --require-version 3.2.2 --strict`
- `gh run view <run-id> --repo LiveTrack-X/DirectPipe`
- `gh release view v4.2.1 --repo LiveTrack-X/DirectPipe`
- Download and verify the published `checksums.sha256`.

## Release Evidence

- Attempt 1: source `809c53e1fd5c5d32b8e56cf00c754599e8c8e858`,
  run `30250350172`. Text/version/tag checks passed, but Stream Deck dependency
  audit failed on newly reported high-severity development-tool advisories in
  `brace-expansion` and `sharp`. Production dependencies reported 0
  vulnerabilities. No GitHub Release was created; the run was cancelled while
  unrelated platform builds were still running.
- Final source/tag commit:
  `258d6ebda559ba39870b611f2d0478e4743969a4`.
- Successful CI: `Build & Release` run
  [`30250688818`](https://github.com/LiveTrack-X/DirectPipe/actions/runs/30250688818).
  Text/tag integrity, Windows, macOS, Linux, Stream Deck, and publish jobs all
  completed successfully.
- Published 2026-07-27 at
  [`DirectPipe v4.2.1`](https://github.com/LiveTrack-X/DirectPipe/releases/tag/v4.2.1)
  as a public, non-prerelease, latest release.
- Downloaded release assets were independently matched to `checksums.sha256`:
  - `DirectPipe-v4.2.1-Windows.zip`:
    `d1ea784c1639d5a41608d7fa4da3f769364afd63d32baec8ba3659f455c66c54`
  - `DirectPipe-v4.2.1-macOS.dmg`:
    `fdfd09fc403b27017bdc8f4fdbf8f4b7af45cb72d4f37d2646a69fe9cd9741b0`
  - `DirectPipe-v4.2.1-Linux.tar.gz`:
    `3c7ae7f168b60648498cf7b31ba6d2c8c8f44dafaa500872ceadfd43c2cd23ee`
  - `com.directpipe.directpipe.streamDeckPlugin`:
    `58a797dffe3372e33f0c341548f5de586eb482bd730b3f877d20fb0db583062d`
- Acceptance remains bounded to the software/CI/package/publication evidence
  above. The declared real-device and hardware/runtime exclusions remain
  unverified.
