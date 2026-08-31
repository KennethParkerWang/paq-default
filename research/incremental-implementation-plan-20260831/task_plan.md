# Task Plan: incremental hybrid implementation and verification

## Goal
Define an implementation order in which every code increment answers one compression/correctness question and can be disabled without affecting the original PAQ route.

## Phases
- [x] Phase 1: Inspect the current archive, arithmetic-stream, detector, and structured-filter integration.
- [x] Phase 2: Separate framing, planning, native transforms, routing, and specialist integration into testable increments.
- [x] Phase 3: Define the test and stop gate for every increment.
- [x] Phase 4: Write the reusable implementation/test route.
- [x] Phase 5: Implement after explicit user authorization.
  - [x] Stage 0: Freeze hybrid archive version, route IDs, lengths, checksums, and decoder contracts.
  - [x] Stage 1: Add the framed outer archive and reversible `RAW_STORED` backend contract.
  - [x] Stage 2: Carry a complete legacy PAQ archive as an opaque backend payload and dispatch legacy inputs.
  - [x] Stage 3: Separate block planning from block encoding and validate the plan structurally.

## Decisions Made
- Preserve the original v216-derived path as a fallback and use a new archive version for hybrid payloads.
- Validate framing before compression logic, PAQ carriage before routing, and one native vertical slice before expanding profiles.
- Integrate one external backend at a time; no all-backend implementation before the common contract is proven.
- Byte-exactness is a mandatory gate. Compression gain is a separate economic gate.

## Errors Encountered
- None. This planning pass did not modify source code or run tests.

## Status
**Stage 0-3 code written.** Per user instruction, this pass performed static inspection only: no build, execution, round-trip, benchmark, or other test was run. Runtime correctness remains unverified until a later authorized test pass.
