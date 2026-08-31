# Hybrid PAQ incremental implementation and test route

## Core rule

Implement one vertical capability at a time. Each stage must pass its correctness gate before the next stage starts. Compression-gain failure disables or narrows that route; it does not block the common container from progressing.

## Stage 0 — Freeze the contract and baseline

**Write**

- No algorithm code yet.
- Freeze new archive magic/version, route IDs, length representation, checksum scope, and compatibility rule.
- Record the exact original PAQ executable/source identity and command settings.

**Test**

- Reuse or record original whole-file and prefix baseline sizes.
- Confirm that the original project remains untouched.

**Gate**

- Every future result can be traced to one source version, executable, command, and input hash.

**Why first**

- Without a frozen baseline, later size differences cannot be attributed to one code change.

## Stage 1 — New outer archive with RAW_STORED only

**Write**

- `HYBRID_ARCHIVE_HEADER`.
- `HYBRID_BLOCK` containing `restored_len`, `payload_len`, `route_kind`, contract ID, optional checksum, and payload.
- Strict length/overflow/resource validation.
- Only one route: `RAW_STORED`.

**Test**

- Empty input; 1 byte; 1 KiB; 32 KiB; lengths around integer encoding boundaries.
- Complete `sao`, `osdb`, `mr`, `x-ray`, and one mixed/TAR file using RAW only.
- Truncated frame, oversized length, unknown route, and corrupted payload rejection.

**Gate**

- 100% byte-identical output for every valid input.
- Every malformed frame fails explicitly without reading into the next frame.

**Why here**

- This isolates framing, concatenation, checksums, and resource limits from PAQ, detectors, transforms, and specialists.

## Stage 2 — Backend interface and PAQ carriage

**Write**

- Stable `Backend` interface and `BackendRegistry`.
- `PAQ_SEGMENT`/`PAQ_GENERIC` adapter that writes a bounded PAQ payload into a temporary sink, then records its exact `payload_len` in the outer frame.
- Legacy archive dispatcher: new decoder reads old archives; old decoder rejects the new magic.

**Test**

- Encode complete files as one PAQ segment and compare restoration with original v216.
- Then split the same input into fixed PAQ segments to quantify context-reset cost separately from framing cost.
- Test unknown backend contract and corrupted PAQ payload.

**Gate**

- Exact restoration.
- One-segment payload differs from the baseline only by understood container overhead/settings.
- Context-reset regression is measured before hybrid routing relies on many PAQ segments.

**Why before planning**

- The hybrid format must first prove it can carry the original compressor. Otherwise any later regression could be caused by PAQ segmentation rather than the new expert.

## Stage 3 — Planner separated from encoder

**Write**

- `BlockPlan`/`SpanPlan`: offset, restored length, semantic hint, optional structure profile, route placeholder.
- Refactor detection into a planning pass that does not immediately encode.
- Coverage validator requiring ordered, non-overlapping spans whose lengths sum exactly to the source length.

**Test**

- Planner-only output for all Silesia files; compression remains forced through the already-proven PAQ route.
- Boundary cases: empty spans, adjacent spans, final partial record, TAR/member boundary, large lengths.

**Gate**

- Planned spans cover every byte exactly once.
- With routing disabled, archive restoration and payload choice remain unchanged.

**Why now**

- Structure discovery and backend selection cannot be debugged while the detector is still directly mutating the encoder stream.

## Stage 4 — First vertical native route: FIXED_RECORD on SAO

**Write**

- Convert the existing record transpose/delta primitives into a `FIXED_RECORD` profile plus explicit reconstruction plan.
- Start with a strict, deterministic SAO-compatible profile; do not claim generic learning yet.
- Route only to `PAQ_NATIVE_GRAPH`; no external codec.

**Test**

- Primitive inverse tests including zero length, one record, partial tail, maximum supported stride, and tile boundaries.
- Beginning/middle/end windows of `sao`, then complete `sao`.
- Compare final archive bytes including frame and recipe against original PAQ.

**Gates**

- Correctness: every case is byte-identical.
- Economic: complete-file net gain is positive by a predeclared margin; otherwise keep the primitive but disable this route.

**Why SAO first**

- SAO has a clear fixed-record structure and the strongest public indication that structural decomposition can beat PAQ. It tests the complete pipeline with the smallest semantic ambiguity.

## Stage 5 — General structure proposer and validator

**Write**

- Bounded record-width/field-layout proposals using cheap deterministic features.
- Exact-coverage and invertibility validator.
- Confidence and minimum-size gates.
- Low-confidence fallback to PAQ Generic.

**Test**

- Positive: SAO and independent fixed-record files.
- Negative/OOD: random, encrypted, already-compressed, text, executables, and irregular binary data.
- Adversarial tails and a single damaged record.

**Gate**

- No correctness failure is tolerated.
- False proposals must remain economically harmless through fallback/side-stream rules.
- A proposal that only works on SAO but fails whole-file holdout is narrowed or removed.

**Why after the hardcoded vertical slice**

- This separates “the transform/backend pipeline works” from “automatic discovery works.” If implemented together, a bad result would not reveal which half failed.

## Stage 6 — Add native profiles one at a time

Order:

1. `NUMERIC_ARRAY`.
2. `STRIDED_2D` for raw/strictly reconstructed pixel regions.
3. `TOKEN_NUMERIC_RECORD`.
4. `REPEATED_TEMPLATE`.
5. `EMBEDDED_COMPRESSED_STREAM` detection only; reconstruction backend comes later.

For each profile:

- Add one profile and one reconstruction recipe.
- Run inverse/edge tests.
- Run representative multi-position samples.
- Run at least one complete target file.
- Run negative/OOD files.
- Keep it disabled if final net gain is not positive.

**Why serially**

- One-at-a-time ablation identifies exactly which profile changes size, speed, routing, or correctness.

## Stage 7 — Cost-aware router and operating modes

**Write**

- Router features and versioned decision table/tree.
- `default`, `balanced`, and `max-ratio` budgets.
- Size estimator including frame, recipe, dictionary/model amortization.
- `RAW_STORED` and PAQ fallback.

**Test**

- Compare chosen route with an offline oracle on the same feasible candidate set.
- Report routing regret, false-positive rate, OOD fallback rate, feature cost, and final bytes.
- Verify deterministic decisions across repeated runs/platform configurations where supported.

**Gate**

- Default mode performs no unrestricted multi-codec race.
- Router overhead and regret remain below predeclared product limits.

**Why after profiles**

- A router cannot be trained or judged until the candidate routes and their true final costs exist.

## Stage 8 — Prove the external-backend mechanism with a simple codec

**Write**

- External/specialist adapter contract.
- Use an already available simple byte-lossless codec only to prove payload dispatch, versioning, error handling, and reconstruction. It need not be enabled as a ratio winner.

**Test**

- Whole-file and mixed PAQ/specialist frames.
- Unknown contract, truncated payload, decoder error, oversized output, checksum mismatch.

**Gate**

- The specialist cannot write outside its bounded output and must produce exactly `restored_len` bytes.
- Failure cannot desynchronize subsequent frames.

**Why use a simple backend first**

- It isolates the backend ABI/contract from the complexity of OpenZL, JXL, Pco, or compressed-stream reconstruction.

## Stage 9 — Add evidence-backed specialists one by one

Recommended investigation order, subject to build/license feasibility:

1. OpenZL-style/compatible SAO fixed-record backend.
2. Exact JPEG reconstruction via JPEG XL.
3. Numeric specialist such as Pco for validated numerical streams.
4. Precomp-style exact reconstruction for selected compressed streams.
5. DICOM pixel-region specialist only after whole-file byte reconstruction is defined.

For every backend:

- Freeze a decoder contract, not a dynamic-library ABI.
- Record build/license/dependency footprint.
- Test exact reconstruction and corrupted-input behavior.
- Compare same input against v216, including every metadata byte.
- Keep the backend opt-in until independent files show stable gain.

**Why not integrate all together**

- Each backend introduces a different correctness definition and dependency surface. Simultaneous integration makes failures inseparable and can lock an unstable archive format.

## Stage 10 — Improve existing non-DEFAULT routes

**Write/test order**

- TEXT/TEXT_EOL only after DEFAULT container/router is stable.
- DEC_ALPHA/EXE native transforms next.
- Existing image/audio routes only with direct same-input evidence; do not replace strong PAQ experts based on cross-corpus claims.

**Gate**

- Each replacement must beat the unchanged legacy route on its target bytes and not regress unrelated types.

**Why last**

- These routes already work and often have strong PAQ models. Changing them while the hybrid infrastructure is unstable expands the debugging surface without proving the DEFAULT thesis.

## Stage 11 — Final validation ladder

1. Unit/inverse tests for every primitive and recipe.
2. Boundary and malformed-frame tests.
3. 32 KiB and multi-position samples for diagnosis.
4. Complete target files for real net gain and byte exactness.
5. Complete Silesia per-file and aggregate report.
6. Independent same-domain files using whole-file holdout.
7. OOD/random/already-compressed fallback suite.
8. P50/P95/max encode time, decode time, peak memory, metadata bytes, and routing regret.

Passing Silesia proves compatibility and gain on Silesia only. Independent same-domain files are required before a route becomes default.

## Why this route is cheaper than “finish everything, then test”

- A Stage 1 failure is definitely a frame/payload bug.
- A Stage 2 regression is PAQ carriage or context-reset cost.
- A Stage 3 failure is span planning.
- A Stage 4 failure is the first native transform/profile.
- A Stage 5 failure is automatic discovery/generalization.
- A Stage 7 failure is routing economics.
- A Stage 8/9 failure belongs to one specialist contract.

The same failure after an all-at-once implementation could originate in any of these layers and force broad rewrites. Incremental gates reduce the number of possible causes at every step while preserving the final architecture.
