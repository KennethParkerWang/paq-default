# Original Stage 0-3 Code Completion

This file records completion against the stage definitions in
`research/incremental-implementation-plan-20260831/INCREMENTAL_IMPLEMENTATION_TEST_PLAN.md`.

## Boundary

- Implementation target: this derived repository only.
- Frozen hybrid-v1 wire contract remains unchanged.
- Later routed-profile v2 work remains present but is outside this pass.
- No build, executable run, round-trip, test, or benchmark is authorized.

## Completion checklist

- [x] Stage 0: contract constants, checksum scopes, compatibility policy, and source identity are explicit.
- [x] Stage 1 infrastructure: strict framing is complete; RAW encoding was subsequently removed by user decision.
- [x] Stage 2: stable backend registry, bounded PAQ carriage, and compatibility dispatcher are complete.
- [x] Stage 3: planning and encoding are separate and BlockPlan coverage is independently validated.

## Stage 0 — frozen identity and contract

Baseline identity used by the existing records:

- Source tree: `F:\paq8px\paq8px-master\paq8px-master`, PAQ8px v216.
- Reference executable: `F:\paq8px\PaqBenchStudio\staging-v1.1.0\paq8px.exe`.
- Reference executable size: 1,383,936 bytes.
- Reference executable SHA-256: `F79343702F596A4FA6C7CC3E25F2FA9C05199EAF11F06655B065F687E5F42533`.
- Recorded comparison setting: level `-1`, one serial repetition, exact 32,768-byte leading prefixes.
- Encode form: `paq8px.exe -1 <input> <archive>`.
- Decode form: `paq8px.exe -d <archive> <restored>`.
- Derived Stage 0-3 checkpoint: `ce65315 feat(hybrid): checkpoint stage 0-3 infrastructure`.

Frozen hybrid-v1 contract in `src/hybrid/HybridFormat.hpp`:

- Magic: eight bytes `PAQXEG\r\n`; archive version 1.0.
- Unsigned big-endian integers; 24-byte archive header; 64-byte frame header.
- Published IDs remain reserved for compatibility: `RAW_STORED = 0x0001`, `PAQ_LEGACY_ARCHIVE = 0x0002`.
- Published contracts remain reserved for compatibility: `RAW_STORED_V1 = 0x00010001`, `PAQ8PX_LEGACY_ARCHIVE_V1 = 0x00020001`.
- Header CRC32 covers the complete fixed header with its CRC field zeroed.
- Payload, decoded output, and recipe CRC32 values cover exactly their declared lengths.
- Stage 0-3 routes require a zero-length recipe.
- Maximum declared frames: 1,048,576; maximum per-field and aggregate decoded/payload data: 1 TiB.

Compatibility policy:

- The current decoder distinguishes routed-v2, hybrid-v1, structured-v217, and original-v216 magic before dispatch.
- Hybrid-v1 route/contract pairs must be registered before any payload is decoded.
- Old PAQ decoders see the new outer magic instead of a PAQ header and reject it; no old bitstream identifier was reused.
- The current default writer emits routed-v2. Hybrid-v1 remains frozen and readable; RAW is decode-only compatibility and PAQ remains writable.

## Stage 1 infrastructure — strict framing

- RAW writing and routing were removed after Stage 0-3 completion. `readRawStoredArchive()` remains only for old hybrid-v1 archives.
- `ArchiveWriter` and `ArchiveReader` provide canonical multi-frame sequential framing.
- Every frame is checked for header CRC, version, flags, reserved fields, lengths, overflow, resource bounds, payload CRC, decoded CRC, and trailing data.
- `BoundedInputFile` exposes exactly `payloadLength` bytes to a backend, preventing it from consuming the next frame header.
- New encoders cannot select `RAW_STORED`, `OPAQUE_STORE`, `RAW_STORE`, or `RAW_ESCAPE`.

## Stage 2 — backend and PAQ carriage

- `Backend` and `BackendRegistry` bind each route to one immutable decoder contract.
- Complete legacy-PAQ carriage remains the only writable hybrid-v1 backend.
- Before a frame is committed, its backend writes to a separate `FileTmp`; the writer checks the actual length and CRC, then copies exactly the declared payload into the outer archive.
- `writePaqLegacyArchive()` and `readPaqLegacyArchive()` name the complete legacy-PAQ contract explicitly.
- The main decoder uses the named PAQ helper for hybrid-v1 and retains direct v216/v217 plus routed-v2 dispatch.

## Stage 3 — planner/encoder boundary

- `PlannedBlock` records source offset, restored/source length, PAQ semantic hint, optional structure profile, route decision metadata, and the forced Stage-3 PAQ route.
- `buildBlockPlan()` finishes detection before `encodeBlockPlan()` starts encoding.
- `BlockPlan::validateCoverage()` is independently callable and checks nonempty spans, metadata, order, gaps, overlap, arithmetic overflow, range bounds, and exact final coverage.
- `seal()` calls the independent validator, and `encodeBlockPlan()` calls `validateForEncoding()` again before emitting any planned block.
- Stage 3 still rejects every non-PAQ legacy route; native experts and route selection begin only after Stage 3.

## Verification status

Static source and diff inspection completed. No whitespace errors were reported by
`git diff --check`; line-ending conversion warnings reflect the repository's
existing Git configuration. Runtime correctness is intentionally unverified:
no compilation, executable run, round-trip, unit test, malformed-input test, or
benchmark was performed in this pass.
