# Task Plan: EXP02 routed-v2 Silesia leading 32 KiB

## Goal
Build Git commit `77b2c328838df32ffe9feafd88adf1363184cbb7` with the canonical OpenZL profile, then compare it with the screenshot-matching original PAQ8px v216 at level `-1` on the exact first 32,768 bytes of all 12 Silesia files, with byte-exact reconstruction checks.

## Phases
- [x] Phase 1: Lock the commit, original executable, corpus fixtures, file order, parameters, and metrics.
- [x] Phase 2: Configure and build the current CMake/OpenZL executable in a fresh ignored build directory.
- [x] Phase 3: Run 24 serial round trips (12 files x original/current), retaining archives and logs.
- [x] Phase 4: Independently audit hashes and result arithmetic, then write `RESULTS.md` and `results.csv`.

## Locked Conditions
- Files: `dickens, mozilla, mr, nci, ooffice, osdb, reymont, samba, sao, webster, x-ray, xml`.
- Input: exact offset-0 bytes `[0, 32768)` from each Silesia source, reusing the SHA-256-locked EXP01 fixtures.
- Config: level `-1`, one serial repetition, no warmup, default codec options.
- Baseline: `F:\paq8px\PaqBenchStudio\staging-v1.1.0\paq8px.exe`, expected SHA-256 `F79343702F596A4FA6C7CC3E25F2FA9C05199EAF11F06655B065F687E5F42533`.
- Candidate: CMake Release build of Git commit `77b2c328838df32ffe9feafd88adf1363184cbb7` plus the compile-only Windows macro-collision rename `ExecutableSpanKind::OPAQUE` -> `OPAQUE_DATA`, with `ENABLE_OPENZL=ON`.
- Primary metrics: actual archive bytes, bpb, byte/SHA-256 reconstruction, and aggregate delta against original v216.
- Secondary observations: single-run encode/decode wall time, reported memory, and routed segment counts where available.

## Safety
- Use a new experiment directory and a new ignored build subdirectory.
- Refuse to overwrite any archive, restored file, log, or result.
- Do not modify the 12 source corpus files, EXP01 fixtures, or either source baseline.

## Errors Encountered
- The first CMake configuration used FetchContent's full-history OpenZL clone. Git transferred no pack data for several minutes, so the configuration was interrupted before source checkout or build. Resolution: fetch the exact approved commit with depth 1 into a separate local dependency checkout, initialize only the frozen zstd/lz4 submodules, and pass it through `OPENZL_SOURCE_DIR` to a fresh build directory.
- The first full compile exposed a Windows SDK macro collision: `windows.h` defines `OPAQUE` as `2`, which replaced the identically named scoped-enum token in `ExecutableLayout.hpp`. The internal enumerator was renamed to `OPAQUE_DATA` while retaining value `0`; the subsequent build completed and a no-op rebuild reported no work.

## Status
**Complete** - all 24 round trips passed, the independent artifact audit found no core discrepancy, and `RESULTS.md` records the final comparison. The only measurement-quality exception is the unusable all-zero OS peak-working-set field; codec-reported memory remains available.
