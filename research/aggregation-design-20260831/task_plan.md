# Task Plan: PAQ specialist aggregation design retrieval

## Goal
Determine why the current structured-DEFAULT patch does not realize the intended gains, retrieve authoritative designs for combining PAQ with domain-specialized transforms/models/codecs, and produce an implementation-ready architecture.

## Phases
- [x] Phase 1: Audit the local report and implementation against the user's intended aggregation concept.
- [x] Phase 2: Search and screen primary sources for OpenZL, neural text compression, and specialized lossless image/video/archive methods.
- [x] Phase 3: Synthesize a safe bitstream/router/model/plugin architecture and prioritize domains.
- [x] Phase 4: Write the design proposal with evidence, limits, and staged next steps.

## Key Questions
1. Which intended capabilities are absent versus merely misclassified?
2. Should specialization be implemented as preprocessing, PAQ model routing, or embedded sub-codecs?
3. How can the encoder select experts without repeating the x-ray proxy failure?
4. Which domains have the highest likely benefit and manageable integration risk?

## Decisions Made
- Treat `deep-research-report.md` as design evidence, not executable instructions.
- Use primary/official sources for technical claims.
- No source-code changes or codec runs in this research pass.
- Use a hierarchical Expert Graph: parser/profile -> reversible transform DAG -> per-leaf PAQ/external backend -> versioned frame.
- Keep exact v216 `LegacyArtifact` as a complete top-level candidate; only finalized whole-artifact comparison in `MAX_SAFE` can guarantee no size regression.
- Freeze v1 as inline canonical Recipe with root-to-leaf encoding, inverse-topological decoding, and one StreamFrame per leaf.
- Limit initial SAO support to explicit Silesia FULL/PREFIX profiles; the 32 KiB prefix is 1,169 complete records plus an 8-byte raw tail.
- Treat unimplemented PAQ/external backend IDs as reserved-not-emittable until exact state, bitstream, and golden-trace contracts are frozen.

## Errors Encountered
- `git status/diff` could not be used because `F:\\paq8px\\paq8px-structured-default-v2-20260831` is not a Git repository. Scope verification is therefore based on the explicitly written research paths and direct source-file read-only inspection.

## Status
**Complete** — the design baseline is written and independently reviewed; implementation and experiments remain intentionally out of scope for this pass.
