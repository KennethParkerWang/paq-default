# Native routed-profile v2 implementation

## Status and boundary

The derived project contains the source-level implementation of the final
research design: deterministic structure recognition, source-aligned routing,
PAQ model continuity across PAQ fragments, and independently decoded external
expert segments. The original PAQ tree was not modified.

This implementation pass intentionally did not compile, run, round-trip, test,
benchmark, or perform a compression experiment. "Implemented" below means that
the source contract and integration are present; it is not a runtime or
compression-ratio claim.

## Active archive path

- Outer magic: `PAQXRP2\n`; archive version: `2.0`.
- Fixed little-endian 64-byte archive header and 128-byte segment header.
- Header CRC32C plus parameter, payload, recipe, and decoded-output CRC32C.
- Canonical segment ids, exact source offsets/lengths, bounded field sizes,
  canonical ULEB128, and strict START/CONTINUE/END state validation.
- Each sealed `CommitUnit` maps to one routed segment in this version.
- Decoding is driven only by archived profile, expert, parameters, state, and
  lengths. The decoder never re-runs structure detection.
- Decoded leaves are emitted in exact source order. ZIP/container structure is
  also kept source-aligned, so this version does not need a second archive
  concatenation or a non-source-aligned reconstruction map.

Native routing is enabled only for a single file at levels 1 through 12 when
LSTM, model training, multiple-file mode, and external model state are absent.
Level 0 and every unsupported option combination retain the complete legacy PAQ
archive path.

## PAQ state semantics

- All PAQ-routed leaves in one native archive share one `PaqModelSession`.
- Main and block predictor/model state persist across those leaves.
- Every PAQ leaf gets a fresh arithmetic `Encoder` payload containing existing
  PAQ block headers and reversible transforms.
- External leaves neither observe nor update the PAQ state. PAQ continuation is
  therefore continuity over the PAQ subsequence, not over all source bytes.
- The PAQ configuration is archived canonically and must remain identical in
  every fragment of the state epoch.
- Native v1 freezes the PAQ path to scalar execution so decoder behavior does
  not depend on a command-line or host SIMD choice.

## Deterministic planner and fallbacks

The source planner applies exact parsers before the existing PAQ block detector:

1. ZIP/ZIP64 stored-member layout.
2. Exact x86/x64 executable layout.
3. Exact full-Silesia SAO schema for the frozen OpenZL expert.
4. Existing PAQ block detection and DEFAULT refinement.

Unknown, ambiguous, malformed, unsupported, resource-limited, too-deep, or
dependency-disabled structures go to PAQ. There is no RAW/STORE route, online
codec race, or post-compression PAQ size candidate.

The planner seals and revalidates exact source coverage before archive emission.
The decoder independently requires monotonically contiguous segment offsets and
the exact archive-level decoded length.

## ZIP and ZIP64

- The parser validates EOCD/ZIP64 records, central/local record agreement,
  optional data descriptors, CRCs, sizes, and resource limits.
- Only stored members with an exact, unambiguous layout are recursively planned.
- Headers, descriptors, central directory, trailer, gaps, and unsupported member
  payloads remain source-aligned PAQ leaves.
- Nested stored ZIP content is opened only to the frozen depth limit; reaching
  that limit is a routing rejection and the complete remaining range uses PAQ.
- Deflate recompression and synthetic ZIP reconstruction are not implemented.

## Executable code

- Exact parsers cover PE/COFF, ELF, and thin Mach-O x86 or x86-64 images.
- Only proven code-file spans use the existing PAQ `EXE` transform, exactly once,
  with the absolute source offset preserved as transform metadata.
- Headers, resources, non-code sections, overlays, unsupported ISA, fat Mach-O,
  and ambiguous/malformed images remain PAQ DEFAULT.

## OpenZL SAO expert

- The first external expert is `OPENZL_FROZEN_V1`, limited to the exact complete
  SAO object used by the Silesia corpus schema contract.
- Parameters freeze expert revision, decoder contract, wire revision 24,
  PlanId, schema id, object count/size, and the implementation descriptor hash.
- The encoder constructs the fixed OpenZL v0.2.0 graph, compresses once, then
  self-decodes and byte-compares the result before the external route can commit.
- Any recognition, resource, encoding, or verification failure falls back to
  PAQ before the segment is written.
- The archive also carries payload/decoded length and CRC32C contracts.

OpenZL v0.2.0 exposes no public API that authenticates a self-described frame's
entire internal graph against the frozen PlanId before decompression. The PlanId
and descriptor hash identify this implementation contract; they are not a
cryptographic proof of the graph embedded in an arbitrary OpenZL payload. The
wire version, declared decompressed size, bounded decode, and outer CRC are
validated around the library decode.

## Build contracts

The canonical full build is CMake:

- `ENABLE_OPENZL=ON` by default.
- OpenZL is pinned to full commit
  `3dceb64867840201fb8f57a29d179995f700c9b8` (v0.2.0).
- A local `OPENZL_SOURCE_DIR` is accepted only when it is a clean Git checkout
  at that exact commit and its version header reports 0.2.0.
- Optional OpenZL tests, benchmarks, tools, examples, Python, and install targets
  are disabled.

Intended commands (documented only; not executed in this pass):

```text
cmake -S . -B build-routed -DENABLE_OPENZL=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-routed --config Release
```

The direct Visual Studio `.sln`/`.vcxproj` configurations and manual GCC, Clang,
AArch64, and MinGW scripts are retained as explicit PAQ-only compatibility
builds and define `PAQ_DIRECT_VCXPROJ_BUILD=1`. Those executables print a startup
warning that SAO/OpenZL routing is disabled and that the canonical CMake build is
required for the full profile. The CMake helper enables OpenZL.

## Decoder and transform hardening

The native decoder applies bounded allocation, exact-read, canonical-VLI, and
transform-depth/scratch limits before trusting archived lengths. The legacy
decoder contract remains compatible, while native v2 additionally rejects
oversized or inconsistent Base64, Base85, zlib, TAR, MRB, RLE, GIF, BMP, PNG,
CD, DEC Alpha, LZW, EXE, and temporary-file restoration states before they can
escape the declared segment bounds.

Before any truncating archive/output creation, the CLI compares normalized
absolute paths and existing file identity. It refuses an output that aliases an
input, input-list member, or archive through path spelling, symlink, or hard
link. Legacy multi-file compression freezes the same archived-name contract
before reading any listed member or creating the archive, so it cannot emit
duplicate, conflicting, or decoder-rejected targets. Extraction preflights the
archived list name and every member before its first output `create`/`open`:
names must be safe relative paths, resolved targets must remain under the
canonical output root, and duplicate, equivalent, or ancestor/descendant output
conflicts are rejected. Windows comparison uses ordinal Unicode case folding;
device names, alternate data streams, trailing-dot/space aliases, and DOS 8.3
alias spellings are also rejected. Existing intermediate targets must be
directories; an existing final target must be a regular file with only one hard
link. Listing performs archive-name syntax checks without touching the
filesystem.

## Deliberately inactive routes

Identifiers and parameter contracts for Pco, Sprintz, ALP, Gorilla, DeXOR,
FLAC, WavPack, JPEG XL, and FFV1 remain reserved only. They are not registered
as working encoders/decoders and cannot be selected. `RAW_STORE`,
`OPAQUE_STORE`, `FULL_SHADOW`, `GLOBAL_LIGHT`, ZIP Deflate recompression, and
generic inferred RECORD-to-OpenZL routing are also not active.

## Main implementation files

- `src/hybrid/RoutedExecution.hpp`: planning, commit preparation, native encode,
  and native decode.
- `src/hybrid/PaqModelSession.hpp`, `PaqConfig.hpp`: persistent PAQ state and
  canonical fragment configuration.
- `src/hybrid/RoutedFormat.hpp`, `RoutedArchive.hpp`, `RoutedIO.hpp`: wire format,
  staged parsing, checksums, and bounded I/O.
- `src/hybrid/ZipParser.hpp`, `ContainerLayout.hpp`: exact ZIP/ZIP64 layout.
- `src/hybrid/ExecutableLayout.hpp`: PE/ELF/thin Mach-O layout.
- `src/hybrid/SaoSchemaParser.hpp`, `OpenZlPlanRegistry.hpp`,
  `OpenZlExpert.hpp`: exact SAO recognition and frozen OpenZL adapter.
- `src/filter/BlockPlan.hpp`, `Filters.hpp`: exact PAQ block coverage and
  range-fragment encode/decode.
- `src/paq8px.cpp`: native/legacy dispatch and compatibility behavior.
- `CMakeLists.txt`, `cmake/OpenZLDependency.cmake`: canonical dependency build.

## Unverified boundary

No successful compilation, executable startup, archive round-trip, malformed
archive run, Silesia comparison, performance measurement, or compression-gain
claim is made for this Stage 4 implementation. Those are the next runtime gates
only if the user later authorizes them. Multi-file target preflight cannot close
the general concurrent-filesystem TOCTOU race without platform-specific
handle-relative creation APIs; no runtime claim is made for that boundary.
