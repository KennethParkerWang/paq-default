# Notes: Structured DEFAULT v2

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
