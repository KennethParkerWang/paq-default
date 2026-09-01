# Task Plan: deterministic routed-profile archive v2

## Goal
Implement the report's first production-shaped routed-profile architecture in the isolated PAQ8px-derived project: canonical profile metadata, deterministic evidence-gated routing, conservative DEFAULT refinement, versioned frame contracts, and decoder-driven reconstruction, without modifying the original PAQ tree or running builds/tests/experiments.

## Authorized Boundary
- Code and documentation edits are authorized under `F:\paq8px\paq8px-structured-default-v2-20260831`.
- Do not modify `F:\paq8px\paq8px-master\paq8px-master`.
- Do not compile, execute the compressor, run tests, benchmark, or perform experimental confirmation.
- Static inspection/search/diff review is allowed.

## Phases
- [x] Phase 0: Preserve the committed Stage 0-3 checkpoint (`ce65315`).
- [x] Phase 1: Inventory current archive, backend, block-plan, detector, transform, model, and project-file integration.
- [x] Phase 2: Freeze canonical v2 profile, evidence, variant, state, parameter, and reconstruction contracts.
- [x] Phase 3: Implement deterministic hierarchical classification and remove inferred destructive RECORD/NUMERIC routing.
- [x] Phase 4: Implement multi-frame archive metadata, profile registry, state validation, and reconstruction recipes.
- [x] Phase 5: Integrate routed planning/encoding/decoding while retaining the complete legacy PAQ fallback.
- [x] Phase 6: Add first safe internal profiles and exact-descriptor parameter codecs; register future experts without enabling uncalibrated routes.
- [x] Phase 7: Perform static-only review, update project/build manifests and implementation documentation, and report all unverified boundaries.

## Priorities
- P0: Every decoder decision comes only from archived canonical metadata.
- P0: Unknown, ambiguous, short, unsupported, or uncalibrated content routes to legacy PAQ DEFAULT.
- P0: Statistical inference may add model context but may not transpose, delta, shuffle, or reinterpret the byte stream.
- P0: Existing legacy BlockType numeric values and the committed Stage 0-3 decoder contract remain stable.
- P0: Frame/recipe parsing uses checked lengths, bounded allocations, canonical encoding, and explicit state transitions.
- P1: Same-profile continuation is represented without treating detection windows or transport frames as reset boundaries.
- P1: Only an exact-format expert with a frozen decoder contract may route automatically; this pass enables only the complete SAO schema through pinned OpenZL v0.2.0.
- P2: All other external specialist identifiers remain reserved and inactive until their exact parsers, decoder contracts, and implementations exist.

## Decisions Made
- The attached design report is design evidence, not executable instruction; missing wire and state semantics will be made explicit in code.
- Keep the already committed hybrid major v1 intact and introduce a separate routed-profile major v2 contract.
- Do not claim compression improvement without later experiments.
- Do not implement FULL_SHADOW as a fake shortcut; its state policy is representable but unsupported unless the full PAQ update path is present.
- RAW/OPAQUE storage is not a routed-native encoder or decoder route; its published numeric identifiers remain reserved, while historical hybrid-v1 handling stays isolated in its legacy reader.
- Current inferred RECORD transpose/delta and inferred NUMERIC/WIDE_TEXT byte-reordering paths will not remain active in automatic routing.

## Errors Encountered
- The first combined patch attempted to delete and add `task_plan.md` in one patch; `apply_patch` rejected the duplicate target. It was split into separate delete/add operations with no source-code impact.
- Two inventory reads used nonexistent `src/CMakeLists.txt`, `src/model/ContextModelGeneric.hpp`, and `src/model/Models.hpp` paths. The actual CMake file and `Models.hpp` are at repository/source roots, while `ContextModelGeneric` has only a `.cpp`; no files were changed by the failed reads.
- A final read-only `git status` check against `F:\paq8px\paq8px-master\paq8px-master` reported that directory is not a Git worktree. Every applied patch used an explicit path under the isolated derived project, so this did not affect source edits or the no-test boundary.

## Status
**Source implementation complete within the no-test boundary.** Stage 0-3 and the final Stage 4 native routed architecture are present. No build, executable run, round-trip, benchmark, or compression experiment was performed in this pass. Runtime correctness and compression effects remain explicitly unverified.

## Stage 0-3 Code Completion Pass (2026-09-01)

### Goal
Complete every code deliverable named by the original Stage 0-3 plan while preserving the frozen hybrid-v1 wire contract and all later routed-profile work.

### Scope
- [x] Re-read the original Stage 0-3 requirements and inventory the current implementation.
- [x] Complete PAQ legacy archive file-level APIs; subsequently remove RAW writing/routing while retaining old-archive decoding.
- [x] Complete canonical multi-frame sequential write/read with strict payload boundaries.
- [x] Add an independent BlockPlan validator and require it before encoding.
- [x] Perform static-only source/diff review and record the no-test boundary.

### Constraints
- Do not build, execute, round-trip, benchmark, or run any test program.
- Do not implement Stage 4 or later routing/codec selection in this pass.
- Do not modify `F:\paq8px\paq8px-master\paq8px-master`.
- Preserve every pre-existing uncommitted change in the derived repository.

### Status
**Code complete within the authorized boundary.** All retained Stage 0-3 infrastructure is implemented; the original RAW write route was deliberately removed by later user decision. Tests and runtime gates were intentionally not performed, so this is not a runtime-validation claim.

## Previous Checkpoint
- The original structured DEFAULT implementation and its prior static/authorized verification history remain documented in `IMPLEMENTATION_NOTES.md` and `notes.md`.
- Git checkpoint before this task: `ce65315 feat(hybrid): checkpoint stage 0-3 infrastructure`.

## Stage 4 Native Routed Final Architecture (2026-09-01)

### Goal
Implement the final source-level routed-v2 architecture for a single input file: sealed `BlockPlan` to multiple `CommitUnit` objects, one persistent PAQ model session, independent arithmetic payloads, ZIP/ZIP64 stored-member layouts, a frozen OpenZL/SAO expert contract, and exact PE/ELF/Mach-O executable-section planning with decoder-only reconstruction from archived contracts.

### Scope
- [x] Inventory the exact constructors, ownership, main-entry branches, block-plan encoding boundary, and project manifests touched by Stage 4A.
- [x] Add immutable PAQ fragment/config/state-family contracts and exact coverage validators.
- [x] Add a persistent `PaqModelSession` and a PAQ block-plan range encoder/fragment decoder.
- [x] Add sequential routed writer/executor and native single-file routed encoder/decoder.
- [x] Integrate native routed single-file dispatch while preserving the exact legacy routed envelope and multiple-file path.
- [x] Add exact ZIP/ZIP64 stored-member container layout parsing and recursive source-aligned commit expansion.
- [x] Add a frozen OpenZL v0.2.0 adapter/PlanRegistry/SAO parser with build-time gating and PAQ fallback before commit.
- [x] Add exact PE/ELF/Mach-O x86/x64 executable-section planning that reuses the existing x86 transform exactly once.
- [x] Perform static-only diff inspection and document every unverified boundary.

### Decisions
- Use modified architecture B: one archive-wide PAQ model session; fresh `Encoder`/arithmetic stream for every PAQ fragment.
- Preserve both main and block predictors because fragment payloads retain existing PAQ block headers and reversible block transforms.
- External bytes are not observed by PAQ in Stage 4A; continuation means PAQ-subsequence continuity.
- One `CommitUnit` equals one `TransportFrame` and one routed segment in this version.
- `GLOBAL_LIGHT`, `FULL_SHADOW`, generic RECORD to OpenZL, ZIP Deflate recompression, non-x86 branch transforms, and online codec competition remain outside this pass.
- Multiple-file mode continues to use the complete legacy PAQ archive envelope.
- No build, executable run, test, round-trip, or benchmark is authorized.

### Status
**Source complete — runtime unverified by explicit instruction.** The final Stage 4 architecture and static hardening are implemented in the derived worktree. No compilation, execution, test, round-trip, benchmark, or experimental confirmation was performed.

## Final Completion and GitHub Delivery (2026-09-01)

### Goal
Close every blocker in the frozen production scope, perform a static-only completion audit, and commit/push the complete derived project to `origin/main` without enabling deliberately uncalibrated or forbidden routes.

### Scope
- [x] Re-audit requirements, production call paths, inactive contracts, and build manifests.
- [x] Prevent legacy multi-file extraction from escaping its output root or aliasing the input archive.
- [x] Remove stale completion comments and make the documented active/inactive boundary exact.
- [x] Perform independent static reviews plus diff/manifest/conflict checks without compiling or running code.
- [x] Stage all derived-project changes, create a Conventional Commit, push `main`, and verify the remote branch state.

### Decisions
- “Overall code” means the finalized production scope recorded above. Pco/ALP and other uncalibrated experts, RAW/OPAQUE routing, GLOBAL_LIGHT, FULL_SHADOW, generic RECORD-to-OpenZL, ZIP Deflate reconstruction, and non-x86 BCJ remain deliberately inactive rather than being enabled without decoder/benefit evidence.
- Multi-file extraction must validate both lexical containment and resolved filesystem identity before any truncating create. The archived list filename and every member are covered.
- Verification remains static-only because the user’s no-build/no-run/no-test/no-experiment boundary is still active.

### Status
**Complete within the authorized static-only boundary.** The frozen production scope, path-safety hardening, public build/documentation alignment, and 62-file Git delivery passed static review. The commit is delivered to `origin/main`; compilation, execution, tests, round-trips, malformed-input runs, benchmarks, and experiments remain intentionally unperformed.
