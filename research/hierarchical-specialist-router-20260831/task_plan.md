# Task Plan: Hierarchical specialist router based on public benchmarks

## Goal
Replace the mistaken per-input dual-encoding/direct-subcodec design with a fast hierarchical PAQ expert fusion: residual/uncertain data remains DEFAULT, recognized non-default data is subdivided, and publicly winning ideas are ported as reversible transforms or probability experts feeding the same PAQ mixer/arithmetic coder.

## Phases
- [x] Phase 1: Restate the corrected target and identify which parts of the prior design must be retained, changed, or removed.
- [x] Phase 2: Search current public benchmark/leaderboard and primary-source evidence across general, text, structured, numeric, image, audio, video, container, executable, database, scientific, and other useful domains.
- [x] Phase 3: Build the hierarchical type/subtype/expert/fallback matrix with speed, byte-lossless, dependency, and small-block constraints.
- [x] Phase 4: Write the corrected implementation design and a plain-language explanation.

## Key Questions
1. Which public results identify useful mechanisms to fuse into PAQ for each recognized subtype?
2. Which methods are file-byte-lossless versus only content-lossless after decoding pixels, samples, or records?
3. How should rare, short, ambiguous, or unsupported inputs remain residual DEFAULT?
4. How can routing stay fast without fully encoding both original PAQ and every specialist candidate?
5. Which additional domains beyond the user's examples are worth first-class subtype routing?

## Decisions Made
- The user's examples are seeds, not the scope boundary; the design must add other high-value domains.
- Runtime full PAQ-versus-specialist double encoding is not the default architecture.
- The public benchmark matrix is an offline design/training input; runtime uses validated parsing and activates one bounded PAQ expert set rather than running multiple complete codecs.
- Prefer mechanism fusion over direct subcodec routing: reversible structure transforms feed PAQ, and predictors contribute probabilities to the existing mixer/coder.
- Direct external bitstreams are exceptional P3 paths only when their gain cannot be retained through fusion and original bytes remain exactly reconstructible.
- Residual DEFAULT remains a real terminal route after every classification stage.
- No source-code changes, compilation, or compression experiments in this pass.

## Errors Encountered
- The prior design overemphasized whole-artifact dual encoding and underemphasized hierarchical non-default specialization; this pass supersedes that routing policy.
- The first corrected pass still described recognized types as direct specialist routes. The user clarified that the intended default is to learn and fuse the specialist mechanism into PAQ; the design has been corrected again.

## Status
**Complete** — corrected design now uses mechanism fusion rather than direct external routing, includes a four-level recognition system, residual DEFAULT at every stage, public evidence, domain matrix, speed constraints, and concrete mapping to the current source tree.
