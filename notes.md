# Notes: Structured DEFAULT v2

## Original Stage 0-3 completion audit (2026-09-01)
- Authoritative stage definition: `research/incremental-implementation-plan-20260831/INCREMENTAL_IMPLEMENTATION_TEST_PLAN.md`.
- Stage 0-3 are the frozen contract, RAW-only outer archive, backend/PAQ carriage, and planner/encoder separation; they are not the later routed-profile phases.
- Existing hybrid-v1 code already freezes numeric route/contract ids, big-endian headers, CRC scopes, and strict header parsing.
- Remaining code gaps at audit start: only single-frame convenience functions existed, RAW and PAQ did not have explicit file-level named entry points, and BlockPlan coverage checks were coupled to construction instead of being independently callable before encoding.
- User explicitly prohibited all tests and program execution for this completion pass.
- Added sequential `ArchiveWriter`/`ArchiveReader` state machines without changing the frozen hybrid-v1 header layout or identifiers.
- Backend encoding now occurs in an isolated `FileTmp`; declared payload length and CRC are checked before frame commitment.
- Decoder backends now receive a `BoundedInputFile` limited to the declared payload, so a backend request cannot consume a subsequent frame header.
- Added explicit legacy-PAQ file-level helpers; the hybrid-v1 compatibility branch now uses the named PAQ reader. The temporary RAW writer helper added during completion was subsequently removed.
- `BlockPlan::validateCoverage()` now independently revalidates every span and exact source coverage; encoding requires a sealed, revalidated plan.
- Static `git diff --check` reported only existing LF-to-CRLF conversion warnings and no whitespace error.
- No build, executable run, test, round-trip, malformed-input check, or benchmark was performed.
- Later user decision removed RAW from all encoder decisions: hybrid-v1 RAW encoding and its writer helper are disabled; routed-v2 rejects OPAQUE_STORE/RAW_STORE/RAW_ESCAPE before writing. Published identifiers and RAW decoding remain solely for backward compatibility.

## Routed-profile implementation request (2026-09-01)
- Input design report: `C:\Users\Administrator\.codex\attachments\e8951fcc-0d66-4a54-bfa6-63a7efd2f2a4\pasted-text.txt`.
- User authorized direct code implementation and explicitly prohibited experiments and tests.
- The implementation target is the isolated derived repository only.
- The committed hybrid-v1 format contains historical RAW_STORED and legacy-archive contract IDs; current encoding permits only legacy PAQ, while RAW remains decode-only.
- The report proposes a larger profile/state/container contract. It therefore requires a separate major-v2 wire contract rather than silently changing v1.
- Static review must distinguish implemented decoder semantics from reserved future expert identifiers and uncalibrated routes.
- `HybridFormat.hpp` major v1 is big-endian, 24-byte archive header plus 64-byte frame header, and supports only one top-level frame in the current caller.
- `Backend.hpp` treats the complete v217 PAQ archive as an indivisible decoder contract.
- `BlockPlan.hpp` rejects every route except `PAQ_LEGACY_ARCHIVE`.
- Existing structured block types can retain decoder compatibility without increasing `BlockType::Count`: RECORD already has MODEL_ONLY; NUMERIC and WIDE_TEXT can reserve explicit model-only metadata while leaving old transform metadata valid.
- Automatic routing will use RECORD MODEL_ONLY and WIDE_TEXT MODEL_ONLY only; old destructive metadata remains decoder-supported for previously created archives.
- The active automatic path now uses fixed integer counts and a frozen ruleset id; the earlier floating proxy helpers remain source-compatible but have no call site in the production decision path.
- Statistical NUMERIC selection and inferred RECORD transpose/delta are absent from the active route. WIDE_TEXT uses original byte order with strict sampled UTF-16/UTF-32 validity and lane evidence.
- Inferred RECORD requires at least 256 KiB, stride >=16, and the same fixed-integer stride decision in four deterministic non-overlapping windows.
- Routed-profile v2 uses a distinct `PAQXRP2\n` outer magic; compression writes inner `paq8pxrp`, while decoding retains old hybrid-v1, `paq8pxsd` v217, and original `paq8px` v216 compatibility.
- The generic v2 reader/writer supports bounded parameters, payloads, reconstruction recipes, segment CRC32C, canonical segment ids, and continuation state validation. This earlier one-segment carriage decision was superseded by the final native routed implementation recorded below.
- Profile ids for Pco, Sprintz, ALP, Gorilla, DeXOR, FLAC, WavPack, JPEG XL, and FFV1 are reserved, but those experts and every uncalibrated automatic route remain disabled.
- This pass performed static search/diff inspection only. It did not build, run, round-trip, benchmark, or experimentally confirm any file.

## Input
- Design reference: `C:\Users\Administrator\Desktop\deep-research-report.md`
- Code baseline: `F:\paq8px\paq8px-master\paq8px-master`

## Static Findings
- `detectText()` already splits DEFAULT regions before special block encoding.
- `ContextModel.cpp` already accepts a fixed record length for DBF and otherwise enables automatic `RecordModel` detection.
- New types must be appended before `BlockType::Count` to preserve all old numeric values.
- `hasTransform(type, info)` is metadata-sensitive, which allows RECORD model-only mode to avoid a payload transform.
- Existing transformed blocks use encode -> decode -> compare. New structured transforms need a separate trusted path so adoption does not depend on runtime round-trip probing.
- Decoder-side classification is unnecessary and undesirable; archived type/info fully determine inverse behavior.

## Independent Design Choices
- Use proportional non-overlapping samples rather than assuming two fixed 64 KiB windows on every accepted block.
- Align record/numeric transform chunks to full structural units; only the final whole-block tail is copied verbatim.
- Keep WIDE_TEXT on ContextModelGeneric in this version.
- Restrict numeric prediction to unsigned 8/16-bit modular arithmetic; 32/64-bit candidates may only use lane shuffle.
- Validate metadata at decode/model entry and reject impossible/reserved combinations.

## Verification Boundary
- The initial implementation pass used static inspection only, per user instruction.
- A later explicit request authorized one exactly 1024-byte build and round-trip test.
- Compression gain and structured-mode runtime correctness are still not claimed because 1 KiB is below the 16 KiB classifier threshold.

## Final Static Review
- Two independent transform/integration reviews found no theoretical-lossless or encoder/decoder synchronization blocker.
- RECORD transpose/delta, NUMERIC shuffle/vertical/Lorenzo, and WIDE_TEXT lane shuffle are bijective for every metadata combination accepted by validation.
- The new trusted-transform path consumes and emits the archived block length without the historical trial-decode probe.
- The original level `-0` path could dereference a null main predictor while decoding headers. The derived project now gives `Encoder` the live `Shared` object directly, so block type/info state is available at every level.
- All six Visual Studio configurations now request C++17, matching CMake and direct GNU/Clang build scripts.
- Classifier sample boundaries are intentionally approximate: a second sample can start at a different candidate row/stride phase than the complete block transform. This can affect selection quality, never invertibility, because the chosen type/info is archived and the decoder does not classify.
- Thresholds and gains remain uncalibrated. The later 1 KiB check covered only the basic archive path, not the structured classifier or transforms.

## Authorized 1 KiB Round Trip
- Toolchain: CMake/Ninja with MinGW GCC/G++ 16.1.0, Release, C++17.
- Build: PASS, 145/145 steps, `paq8pxsd.exe` linked successfully.
- Input: exactly 1024 bytes, SHA-256 `C7C9D69EC4EB21A5BAC1E72F4B92B83318FC101F2DD08CFB70D070B84893438B`.
- Compression: PASS at level 1, exit code 0, 63-byte archive.
- Decompression: PASS, exit code 0, exactly 1024 restored bytes.
- Exact comparison: PASS, first differing offset `-1`; restored SHA-256 equals the source SHA-256.

## Remembered Silesia Baseline
- The user supplied the EXP00 original PAQ8px v216 level `-1` screenshot and described it as the first 32 KiB of every Silesia file.
- The screenshot and exact visible table transcription are stored under `verification/silesia/baseline/`.
- Later provenance inspection proved that the exact values came from centered 32 KiB slices with nonzero offsets. Use `EXP00-original-v216-32KiB.md` only as the immutable center-slice record; leading-prefix comparisons must rerun the original executable on offset-0 fixtures.
- Do not confuse the center-slice table's derived 2.045369 weighted bpb with the screenshot's cross-tab 1.881 bpb summary.

## Authorized Silesia Leading-Prefix Comparison
- Fair input: offsets 0..32,767 of all 12 Silesia files, level `-1`, one serial repetition, both the screenshot-matching original v216 binary and the derived v217 binary.
- Exact reconstruction: original 12/12 PASS; derived 12/12 PASS.
- Original total: 97,555 bytes, 1.984762 bpb.
- Derived actual total: 98,174 bytes, 1.997355 bpb; +619 bytes / +0.634514%.
- After subtracting the derived archive magic's fixed two-byte-per-file overhead: +595 bytes / +0.609912%.
- RECORD selected `sao` stride 28 mode 0 and improved the normalized result by 22 bytes.
- RECORD selected `x-ray` stride 2 mode 2 and worsened the normalized result by 620 bytes, dominating the aggregate regression.
- NUMERIC and WIDE_TEXT were not selected in this corpus slice.
- Full report: `verification/silesia/EXP01-paq8pxsd-v217-32KiB/RESULTS.md`.

## Stage 4A Native Routed Fragment Implementation
- User authorized direct code changes and explicitly prohibited compilation, execution, tests, round-trips, and experiments.
- Target repository: `F:\paq8px\paq8px-structured-default-v2-20260831`; original v216 reference remains read-only.
- Architecture is fixed to one persistent PAQ main/block predictor session with a fresh arithmetic `Encoder` for each source-level PAQ fragment.
- Fragment payloads retain existing arithmetic-coded PAQ block headers and block transforms, but omit inner archive magic/settings, file list, and file-size prefix.
- Stage 4A is single-file native routed only. Multiple-file archives continue through `writeSingleLegacyArchive()`.
- The user subsequently removed the minimal-change constraint and requested the final architecture in this pass. The implementation scope now also includes ZIP/ZIP64 stored-member layouts, the frozen OpenZL/SAO contract and adapter, and exact executable-section planning. Unsupported or dependency-disabled routes still deterministically fall back to PAQ before segment commitment.

## Final Stage 4 Source Completion (2026-09-01)
- Native routed encoding is active only for eligible single-file levels 1-12; level 0, multiple-file mode, LSTM, training modes, and external model state retain the complete legacy archive path.
- `SourcePlanner` now lowers exact source coverage to ordered commit units: recursively plannable ZIP/ZIP64 stored members, exact x86/x64 PE/ELF/thin-Mach-O code spans, exact full-Silesia SAO schema, and existing PAQ-detected leaves.
- Every PAQ commit uses one persistent `PaqModelSession` and a fresh arithmetic payload. External leaves do not advance PAQ state; START/CONTINUE/END describes the PAQ subsequence across the archive.
- The native PAQ contract freezes scalar execution and archives a canonical PAQ configuration and compatibility hash. Decoder state is created from archive parameters, never command-line detection.
- OpenZL support is pinned to v0.2.0 commit `3dceb64867840201fb8f57a29d179995f700c9b8`; the exact SAO expert self-decodes and byte-compares before commitment. Failure or unavailable dependency returns to PAQ before writing.
- ZIP structure, non-code executable bytes, and all unsupported/ambiguous content remain source-aligned PAQ leaves. Decoder output is direct ordered concatenation; no second container assembly is needed.
- The public OpenZL v0.2.0 API cannot prove before decode that an arbitrary self-described payload graph equals the frozen PlanId graph. The implementation therefore validates the frozen encoder descriptor, wire revision, declared size, resource bounds, decoded length, and outer CRC, without overstating PlanId as wire-graph authentication.
- Native decode hardening added exact reads, canonical bounded VLIs, arithmetic/length checks, recursion/scratch limits, and bounded restoration for legacy transforms used inside PAQ fragments. Legacy decoding retains its compatibility policy.
- The canonical full expert build is CMake with `ENABLE_OPENZL=ON`. Direct `.vcxproj` builds are explicitly PAQ-only and print a startup warning instead of silently omitting the expert.
- Static review found no source-offset coverage mismatch across nonzero-offset PAQ blocks, executable spans, or nested ZIP spans. This remains a static conclusion only.
- Final path-safety review found that `FileDisk::create()` could truncate an input when an explicit output named the same file through another spelling, symlink, or hard link. `paq8px.cpp` now checks existing-file identity plus normalized absolute paths before creating a compression archive or single-file extraction output; multi-file compression checks the list file and every member.
- Base64 restoration now zeroes the missing bytes of a final partial decoded group instead of reusing the previous group; this removes an uninitialized/stale-byte ambiguity while preserving padded Base64 semantics.
- Base64/Base85 metadata now preserves both the upper six bits of the restored length and the independent newline flag; large transformed blocks no longer lose length information when the flag is set.
- Zlib restoration now handles a valid zero-byte payload by issuing the required empty-input `Z_FINISH`, drives non-empty recompression through `Z_STREAM_END`, and emits exactly the frozen original compressed-length prefix while still consuming the full transformed input.
- EXE inverse-transform bounds now compare offsets against the full 64-bit block length without narrowing the length to signed `int`.
- Three final independent read-only reviews found no statically definite P0/P1 in the native entry-to-codec call chain, routed wire/state/reconstruction contracts, or build manifests. All 25 `src/hybrid/*.hpp` headers are listed in both direct Visual Studio project manifests; the CMake OpenZL macro and link conditions are paired.
- No compilation, executable run, unit/integration test, archive round-trip, malformed-input execution, Silesia benchmark, or compression experiment was performed during Stage 4, per the user's explicit instruction.

## Final Delivery Audit (2026-09-01)
- Legacy multi-file compression validates the archived list/member name set before reading any member or creating the archive; extract/compare parses and validates the full target set before the first output create/open.
- Archive-controlled targets must be safe relative paths below the canonical output root. Archive aliases, cross-target aliases, duplicate targets, ancestor/descendant conflicts, dot components, drive/UNC spellings, and Windows device/ADS/trailing-dot-or-space/DOS-8.3 aliases are rejected; Windows target equality uses ordinal Unicode case-insensitive comparison.
- NUL and 0xFF are rejected throughout a file list so the encoder cannot emit content that the ASCIIZ legacy list framing truncates or its decoder rejects.
- Preflight rejects an existing non-directory intermediate component and any existing final directory, FIFO, device, or other non-regular target; existing regular targets with more than one hard-link name are also rejected before earlier outputs can be touched.
- `DoList` validates archived path syntax without resolving or opening output targets. Safe `DoExtract` and `DoCompare` targets are cached as normalized absolute paths before later legacy processing.
- Filesystem preflight does not eliminate a concurrent replacement race between validation and creation. Closing that boundary requires platform-specific handle-relative/no-reparse creation and is not claimed here.
- GitHub-facing documentation now identifies `paq8pxhy` v219 and `.paq8pxhy219`, describes the exact native/legacy route boundary, and marks the current implementation as runtime-unverified.
- The CMake helper enables the pinned OpenZL profile. Visual Studio and direct GCC/Clang/AArch64/MinGW helpers are explicitly PAQ-only, produce `paq8pxhy`, define the warning macro, and no longer silently present themselves as the full build.
- Final completion remains static-only: no compilation, executable startup, test, round-trip, malformed archive execution, corpus comparison, benchmark, or compression experiment is part of this delivery.
