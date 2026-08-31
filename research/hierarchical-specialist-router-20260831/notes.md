# Notes: Hierarchical specialist router and public benchmark evidence

## Corrected User Intent

- DEFAULT is not supposed to disappear. Secondary classification should extract more recognized structures, but rare, short, ambiguous, and unsupported data remains DEFAULT and continues through PAQ's generic path.
- A recognized non-default class is not a terminal label. It must be subdivided again into formats/subtypes, then mapped to a stronger domain-specific transform/model/codec.
- The user's examples are illustrative. The design must proactively cover additional domains.
- Public benchmark and primary-source evidence should determine which mechanisms enter the expert registry offline.
- Runtime should normally route once. Full original-PAQ versus specialist double encoding is too slow and should not be the default.
- Recognized types should normally remain inside one PAQ-coded stream: learn the specialist's reversible representation or probability model instead of wrapping its complete codec.

## Prior Design Elements to Retain

- Byte-exact reconstruction as the lossless criterion.
- Decoder executes recorded decisions and does not reclassify.
- Versioned expert IDs and deterministic inverse transforms.
- External codecs must have a stable decoder/version contract.

## Prior Design Elements to Replace

- Replace global `MAX_SAFE` multi-encode as the main router with hierarchical parsing and a benchmark-informed expert activation table.
- Keep full comparative encoding only as an offline training/oracle tool or a rare bounded ambiguity mode.
- Reorganize the architecture around residual DEFAULT plus recursively specialized non-default branches.
- Demote direct external codec payloads to exceptional later-stage compatibility paths.

## Sources

### Silesia Open Source Compression Benchmark

- URL: https://mattmahoney.net/dc/silesia.html
- Updated public table: programs current as of 2026-05-20; files compressed individually.
- `paq8px_v215 -12L` is the best listed total at 27,825,511 B.
- The per-file columns show why hierarchical routing is still useful:
  - PAQ: `webster` 4,401 KB, `xml` 245 KB, `sao` 3,723 KB, `x-ray` 3,503 KB.
  - `precomp | cmix v21`: `webster` 4,271 KB and `xml` 233 KB, but a worse aggregate total and slightly worse `sao`.
- Conclusion: PAQ is the strong residual/default backbone, but not the per-subtype winner.

### OpenZL SAO

- URLs: https://openzl.org/ and https://engineering.fb.com/2025/10/06/developer-tools/openzl-open-source-format-aware-compression-framework/
- Official full-SAO result: roughly 3.52 MB / 2.06x, versus the public PAQ Silesia row's truncated 3,723 KB.
- Official page reports 203 MB/s compression and 822 MB/s decompression for its SAO profile, compared there with zstd and xz.
- Direct route implication: validated full Silesia-SAO profile is a proven specialist replacement; a 32 KiB prefix is a separate size/profile bucket and is not proven by the full-file result.

### Large Text Compression Benchmark

- URL: https://www.mattmahoney.net/dc/text.html
- Updated 2026-08-30; enwik9 score includes the decompressor/resources.
- Public totals: `fx2-cmix-transformer` 96,996,198 B; NNCP v3.2 107,261,318 B; CMIX v21 108,244,767 B; `paq8px_v206fix1 -12L` 125,099,359 B.
- These are large-Wikipedia/XML results, not a license to send every short text block to a Transformer. Memory, model dependency, and minimum-size buckets are mandatory routing fields.

### Lossless Raw Image Benchmark

- URL: https://github.com/WangXuan95/Image-Compression-Benchmark
- Strictly lossless raw PNM pixels across CLIC2021, LPCB, GDCC2020, UCID, ImgInfo, and Kodak; records size and single-thread encode/decode time.
- Ratio leaders vary by corpus (e.g. EMMA, BMF, Gralic); JPEG XL and WebP occupy more practical speed/ratio regions.
- PAQ is not included, and inputs are raw PNM pixels. Therefore this evidence creates image-expert candidates, not an automatic original-PNG/JPEG file-byte replacement.

### JPEG XL

- URL: https://research.google/pubs/benchmarking-jpeg-xl-lossylossless-image-compression/
- Reports about 22% storage savings while allowing byte-for-byte reconstruction of legacy JPEG.
- This is a strong direct file-byte-lossless route for validated legacy JPEG, unlike decode-PNG-to-pixels and re-encode.

### Numeric/Columnar Data

- URL: https://github.com/pcodec/pcodec/blob/main/docs/benchmark_results.md
- On its public taxi numeric columns, Pco reports 6.98x at level 12 versus 5.32x for max-level Parquet+Zstd; datasets and result CSVs are published.
- No same-corpus PAQ result is provided, so Pco is a high-priority numeric candidate, not yet a proven PAQ replacement.

### Logs

- URLs: https://docs.yscope.com/clp/main/ and https://www.usenix.org/conference/osdi21/presentation/rodrigues
- CLP is a lossless structured/unstructured log compressor that separates templates and variables and reports gains over general compressors.
- LogLite public VLDB results report 8.1% average improvement over the next log-specific method on JSON and 37.7% over LZMA.
- Neither public source directly compares the same data with PAQ8PX; route status remains candidate pending directly comparable evidence.

### Scientific Arrays and Time Series

- URLs: https://gmd.copernicus.org/articles/12/4099/2019/, https://github.com/dblalock/sprintz, and https://arxiv.org/abs/2608.00168
- Shuffle/bitshuffle plus typed codecs can strongly improve numeric scientific data; Sprintz targets multivariate integer time series; a 2026 PETRA III study finds heterogeneous per-category policies outperform uniform policies.
- These sources support deeper subtyping, but do not establish a universal PAQ replacement.

### Lossless Audio and Video

- Audio: https://www.xiph.org/flac/comparison.pdf
- Video: https://compression.ru/video/codec_comparison/pdf/msu_lossless_codecs_comparison_2007_eng.pdf
- Audio results form a speed/ratio frontier rather than one universal winner. The video comparison recommends YULS for ratio, FFV1 for a speed/ratio compromise, but is old and lacks PAQ comparison.
- These are applicable to raw PCM/raw frames after exact container parsing, not to arbitrary already-compressed FLAC/H.264 bitstreams.

### Genomics

- URL: https://pmc.ncbi.nlm.nih.gov/articles/PMC8388020/
- Genozip covers FASTQ, SAM/BAM/CRAM, VCF, FASTA and related formats with public comparisons.
- Its own losslessness documentation warns that gzip/CRAM wrapper bytes may differ after reconstruction. It is not an original-file-byte replacement unless the exact wrapper is separately preserved or the input is a supported raw textual form.

### Columnar Formats and Graphs

- URLs: https://www.microsoft.com/en-us/research/uploads/prod/2024/02/p3044-liu.pdf and https://arxiv.org/abs/2605.21510
- Parquet/ORC results reinforce type-aware dictionary, RLE, delta and bit-packing routes. WebGraph/BVGraph results apply to parsed adjacency lists, not arbitrary graph file bytes.

## Evidence Labels

- `PROVEN_REPLACE`: same corpus/input semantics include a PAQ result or otherwise provide a direct byte-compatible superiority result.
- `PUBLIC_CANDIDATE`: public specialist benchmark is strong but lacks a same-corpus PAQ comparator.
- `STRUCTURAL_ONLY`: the source validates parsing/transform ideas but not a ratio win.
- `INELIGIBLE_FILE_BYTES`: only pixels/samples/logical records are preserved, not the original input bitstream.

## Primary mechanisms selected for fusion

- OpenZL's useful unit is a reversible graph node, not the OpenZL bitstream. For SAO it separates the header and record table, converts AoS to six homogeneous field streams, uses delta for SRA0, transpose/range structure for SDEC0, and tokenize/dictionary-index streams for IS/MAG/XRPM/XDPM. The official result is 3,516,649 B on full SAO.
- Pco's useful sequence is mode-to-latents, delta/delta2/lookback, then entropy bin plus exact offset. The PAQ-native form should encode bin-id and exact-offset as separate logical streams or contexts using the PAQ coder rather than importing tANS.
- CLP splits a log line into log type/template, variable values and timestamp; variables are further separated into dictionary and non-dictionary values. These become PAQ text/dictionary/numeric streams.
- FLAC RFC 9639 contributes channel decorrelation, fixed/LPC integer prediction, residuals, zigzag and partition-local scale selection. The PAQ-native form keeps the reversible residual representation but lets PAQ model/coder encode it.
- FFV1 RFC 9043 contributes plane/slice separation, the median predictor `median(l,t,l+t-tl)` and contexts built from neighboring differences.
- JPEG XL Modular contributes reversible palette/color transforms, self-correcting spatial prediction and signaled context trees. It is a source of image experts, not a license to replace original PNG bytes with pixels.
- Sprintz contributes lightweight online prediction, residual bit-width/bitpacking signals and zero-run handling for small multivariate integer blocks.

## Classification and recognition design

- Separate `FormatId` (byte grammar and boundaries), `ProfileId` (homogeneous data semantics), and `RecipeId/ExpertSet` (what PAQ runs).
- Recognition has four levels: container/boundary parser, known-format internal profiling, DEFAULT secondary structure discovery, then recipe selection.
- Non-DEFAULT blocks are not terminal. They are profiled again: WAV -> PCM format/channels; image -> raw vs compressed bitstream and geometry; TAR -> members; text -> natural/XML/JSON/source/wide text.
- Evidence tiers:
  - E3 exact parser: semantic split and reversible graph allowed.
  - E2 multi-window stable structure: only universally bijective transforms with encoded parameters plus experts.
  - E1 soft hint: probability experts only.
  - E0 unknown/conflict/short/rare: residual DEFAULT.
- PRONOM/DROID provides a useful reliability order: extension is least reliable, binary signature more reliable, container signature most reliable. A magic match alone is still insufficient for a transform; length/offset/version/checksum/terminator invariants must close.
- Decoder never classifies. It validates and executes the recorded, versioned recipe.

## Current-code implications

- `Filters.hpp` currently performs secondary classification only after text detection returns DEFAULT. This must become an all-terminal profile pass so existing recognized types can be subdivided.
- `DefaultStructureDetector.hpp` currently hard-selects among only RECORD/NUMERIC/WIDE_TEXT from two windows. The replacement needs a parser/profile registry, evidence tiers and an explicit residual DEFAULT outcome.
- `StructuredDataFilter.hpp` currently represents a single physical transform. The next design requires multi-stream field graphs.
- `ContextModel.cpp` routes RECORD/NUMERIC/WIDE_TEXT through the default switch arm, so current changes do not create real specialized probability models.
- `ContextModelGeneric.cpp` already supplies the correct aggregation primitive: multiple model outputs enter one mixer. New domain mechanisms should fit that interface.
