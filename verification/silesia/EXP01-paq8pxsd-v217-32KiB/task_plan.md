# Task Plan: EXP01 paq8pxsd v217 Silesia 32 KiB comparison

## Goal
Run like-for-like PAQ8px v216 and paq8pxsd v217 level `-1` round trips on the first 32,768 bytes of all 12 Silesia files, verify both reconstructions, and compare compressed bytes/bpb.

## Phases
- [x] Phase 1: Resolve and fingerprint the 12 source files, executable, and immutable EXP00 baseline.
- [x] Phase 2: Materialize exact 32,768-byte prefixes without modifying the source corpus.
- [x] Phase 3: Compress and decompress every prefix serially, retain logs, and verify exact bytes.
- [x] Phase 4: Calculate per-file/aggregate deltas and write the reproducible result report.

## Key Questions
1. Do all 12 restored prefixes match byte-for-byte?
2. How do compressed bytes and bpb change against EXP00?
3. Which PAQ block types are selected by the derived classifier?

## Locked Conditions
- Files: `dickens, mozilla, mr, nci, ooffice, osdb, reymont, samba, sao, webster, x-ray, xml`.
- Prefix length: exactly 32,768 bytes per file.
- Variant: `-1`; one repetition; serial execution.
- Original executable: screenshot-matching `F:\paq8px\PaqBenchStudio\staging-v1.1.0\paq8px.exe`, SHA-256 `F79343702F596A4FA6C7CC3E25F2FA9C05199EAF11F06655B065F687E5F42533`.
- Derived executable: current Release `verification/build/paq8pxsd.exe`.
- The supplied EXP00 screenshot is retained as a center-slice reference and is not used numerically for this leading-prefix comparison.
- Primary metrics: compressed bytes, bpb, and exact reconstruction. Timing/RAM are secondary observations.

## Errors Encountered
- The supplied screenshot was described as leading-prefix data, but its 12 compressed sizes exactly match `F:\paq8px\benchmark_paq8px_32KiB_parallel`, whose manifest proves nonzero centered-slice offsets. Resolution: rerun both original v216 and derived v217 on the same offset-0 fixtures.
- The first attempt to replace the unexecuted harness used delete/add operations for one path in a single patch, which the patch tool rejected. Resolution: delete and add were applied as two separate patches; static PowerShell parsing reports zero errors.
- The first formal launch stopped before producing artifacts because Windows PowerShell promoted the original codec's normal stderr version banner to a terminating `NativeCommandError` under `ErrorActionPreference=Stop`. Resolution: invoke both codecs through `System.Diagnostics.Process`, capture stdout/stderr independently, and decide success solely from the native exit code plus exact reconstruction.

## Status
**Complete** — 24/24 exact round trips passed; result arithmetic and artifacts were independently audited, and `RESULTS.md` records the comparison and limits.
