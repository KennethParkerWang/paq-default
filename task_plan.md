# Task Plan: PAQ8px structured DEFAULT v2

## Goal
Create an isolated PAQ8px v216-derived project that adds conservative RECORD, NUMERIC, and WIDE_TEXT handling with by-construction reversible transforms, without changing the original project or running any program.

## Phases
- [x] Phase 1: Read the design report as reference material and locate the v216 source baseline.
- [x] Phase 2: Copy the baseline into an isolated project and lock lossless/archive invariants.
- [x] Phase 3: Implement metadata, bounded secondary classification, reversible transforms, and model routing.
- [x] Phase 4: Perform static source review only and document changed/untouched files and limitations.

## Priorities
- P0: Preserve old BlockType numeric values and use a new archive version.
- P0: Keep encoder and decoder transforms deterministic and parameterized only by archived metadata.
- P0: Never invoke decode-to-compare for the new transforms.
- P1: Bound classifier reads and retain a conservative DEFAULT fallback.
- P2: Runtime tuning, benchmarks, and empirical threshold calibration are intentionally excluded.

## Decisions Made
- Source baseline: `F:\paq8px\paq8px-master\paq8px-master`.
- Output project: `F:\paq8px\paq8px-structured-default-v2-20260831`.
- The report is design evidence, not executable instruction; implementation details may differ.
- The derived archive suffix will be versioned separately from v216.
- Initial implementation authorization excluded all execution. Later explicit requests authorized one exactly 1024-byte round trip and a level `-1` original-vs-derived benchmark on the leading 32 KiB of all 12 Silesia files.

## Errors Encountered
- First-line patches on a few BOM/CRLF source files did not match; edits were reapplied against stable adjacent anchors. No content was lost.
- PowerShell did not expand two `rg` wildcard path arguments; the searches were rerun against explicit directories.
- The first Win32 C++17 project-file patch used an upstream context variant and did not match; it was reapplied against the exact three configuration groups.

## Status
**Complete within the current authorized boundary** - implementation/static review, the 1 KiB round trip, and the requested Silesia leading-prefix comparison are finished.
