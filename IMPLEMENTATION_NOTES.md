# Structured DEFAULT v2 implementation notes

## Scope and identity

- Baseline: `F:\paq8px\paq8px-master\paq8px-master` (paq8px v216).
- Derived project: `F:\paq8px\paq8px-structured-default-v2-20260831`.
- Program/archive identity: `paq8pxsd` / version `217` / default suffix `.paq8pxsd217`.
- Archive magic is distinct from v216 because increasing `BlockType::Count` changes predictor state even when no new type is selected.
- The arithmetic coder, mixer, SSE/APM path, and existing special-format priority are unchanged.

## Classification flow

```text
existing format detector
  -> existing DEFAULT/TEXT split
  -> only remaining DEFAULT region
  -> two deterministic non-overlapping bounded windows
  -> RECORD / NUMERIC / WIDE_TEXT family candidates
  -> conservative score and family margin
  -> hard BlockType + blockInfo, or DEFAULT
```

The classifier is encoder-only and reads at most 128 KiB per candidate block. It restores the input file position on every return. The decoder never classifies or guesses parameters.

## Appended block types

All v216 values stay unchanged (`TARHDR == 29`). New values are appended:

```text
RECORD    = 30
NUMERIC   = 31
WIDE_TEXT = 32
```

## Metadata layouts

### RECORD

```text
bits  0..11  stride in bytes, 2..4095
bits 12..13  0 MODEL_ONLY, 1 TRANSPOSE, 2 TRANSPOSE_DELTA
bits 14..31  reserved, must be zero
```

`MODEL_ONLY` leaves the byte stream unchanged and passes the fixed stride to `RecordModel`. Transposed modes use Generic routing with automatic record detection because the original AoS stride no longer describes the transformed bytes.

### NUMERIC

```text
bits  0..15  row width in elements; zero is allowed only for BYTE_SHUFFLE
bits 16..17  log2(element bytes): 0/1/2/3 -> 1/2/4/8 bytes
bit      18  big-endian flag
bits 19..21  1 BYTE_SHUFFLE, 2 VERTICAL, 3 LORENZO, 4 LORENZO_SHUFFLE
bits 22..31  reserved, must be zero
```

Prediction is accepted only for 1- and 2-byte unsigned elements. Four- and eight-byte elements are shuffle-only. A row is limited to 64 KiB. One-byte data cannot carry the endian flag.

### WIDE_TEXT

```text
bits 0..2   actual code-unit width, 2 or 4 bytes
bits 3..31  reserved, must be zero
```

This version recognizes only ASCII-dominant UTF-16/UTF-32-like lane structure and keeps Generic routing. It does not perform Unicode transcoding.

## Why the mappings are lossless by construction

- RECORD transpose and byte shuffle are finite byte permutations. The inverse writes each lane/record index back to its unique original index.
- RECORD delta stores the first byte of each transposed lane and then unsigned modulo-256 differences. The inverse uses the same tile boundary and an unsigned prefix sum.
- Vertical prediction stores `current - up` modulo 2^8 or 2^16. The first row uses prediction zero. The inverse reconstructs the previous row before it is needed.
- Lorenzo prediction uses fixed boundaries and `left + up - upLeft` in unsigned modular arithmetic. The inverse visits row-major order, so all predictor inputs are already reconstructed.
- `LORENZO_SHUFFLE` applies two bijections in sequence and reverses them in reverse order.
- Transform tiles are at most 64 KiB and align to a complete record, element, or row. Encoder and decoder derive identical boundaries from block length and archived metadata.
- A final incomplete record/element/row is copied byte-for-byte.
- Every mapping consumes and emits exactly the archived block length.

Invalid modes, ranges, reserved bits, row geometry, block types, or inconsistent metadata call `quit()` before structured payload decoding.

## Deliberate differences from the reference report

- Window size scales down for 16–128 KiB blocks instead of assuming two fixed 64 KiB windows.
- The scoring proxy uses smoothed adaptive order-0/order-1 costs, explicit complexity penalties, and a cross-family winning margin; report constants were not copied verbatim.
- Fixed record stride is used only for the untransposed model-only path.
- Tiles are aligned to whole structural units so phase does not drift at 64 KiB boundaries.
- The archive magic, not only the filename extension, separates this bitstream from v216.
- Debug CSV logging, experimental switches, tests, benchmarks, and empirical threshold tuning are intentionally absent from this delivery.

## Runtime verification boundary

Existing paq8px transforms still use their historical encode -> decode -> compare safety path. The three new structured types take a separate trusted path and never use that correctness probe. A transformed-size equality check remains as an internal format invariant; it is not a trial decode and cannot select a fallback codec.

The initial delivery constraint prohibited compilation and execution, so the implementation was first accepted only by static mathematical review. Later explicit requests authorized an exact 1024-byte round trip and a comparison on the leading 32 KiB of all 12 Silesia files. Every original and derived reconstruction passed byte-for-byte. The Silesia run exercised RECORD modes 0 and 2, but not RECORD mode 1, NUMERIC, or WIDE_TEXT.

## Static review result and remaining limits

Independent static reviews found no blocking losslessness, metadata, encoder/decoder symmetry, or bounded-buffer defect. They also confirmed that all build descriptions include the two new headers and consistently select C++17.

The derived branch additionally makes `Encoder` own a non-null reference to the initialized `Shared` state. Header type/info handling no longer depends on `predictorMain`, which is absent at level `-0`; higher levels retain the same shared object and state order.

Classifier proxy windows deliberately reset local histories and are not candidate-phase-aware at the second sample. This can overestimate or underestimate compression benefit for a particular stride or row width. It cannot change decoded bytes: classification is encoder-only, and the selected validated metadata is stored in the archive and drives the deterministic inverse transform.

## Authorized 1 KiB verification

- Release build with CMake/Ninja and MinGW GCC/G++ 16.1.0: PASS (145/145 build steps).
- Input and restored size: 1024 bytes each.
- Archive size: 63 bytes at compression level 1.
- Source and restored SHA-256: `C7C9D69EC4EB21A5BAC1E72F4B92B83318FC101F2DD08CFB70D070B84893438B`.
- Exact positional byte comparison: PASS, no differing offset.
- Full evidence: `verification/1k/RESULT.md`.

## Authorized Silesia leading-prefix verification

- Inputs: offsets 0..32,767 of all 12 Silesia files, level `-1`, one serial repetition.
- Original and derived reconstruction: 24/24 exact round trips PASS.
- Original v216: 97,555 archive bytes, 1.984762 bpb.
- Derived v217: 98,174 actual archive bytes, 1.997355 bpb, a 619-byte (+0.634514%) regression.
- Removing the derived format's fixed two-byte-per-archive magic overhead still leaves a 595-byte (+0.609912%) regression.
- `sao` RECORD stride 28 / mode 0 improves by 22 normalized bytes.
- `x-ray` RECORD stride 2 / mode 2 regresses by 620 normalized bytes and dominates the total.
- Detailed artifacts and limitations: `verification/silesia/EXP01-paq8pxsd-v217-32KiB/RESULTS.md`.
