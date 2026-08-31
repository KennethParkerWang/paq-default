# Notes: PAQ specialist aggregation design retrieval

## Local Evidence

- The local report explicitly scoped its direct implementation task to a conservative first stage: secondary classification of `DEFAULT` into `RECORD`, `NUMERIC`, and `WIDE_TEXT`, with reuse of existing PAQ models and no arithmetic-coder or mixer redesign.
- Existing image, JPEG, PNG, MRB, ZLIB/GIF/LZW/RLE/Base64/Base85, TAR, and executable paths were intentionally left outside this first-stage refactor. Text/neural work was deferred to later priorities.
- OpenZL's SAO result was used as an oracle/reference for latent structure, not as a requirement to embed OpenZL or reproduce its field-aware compression graph.
- The current implementation therefore does not realize a general specialist aggregation architecture. `RECORD`, transformed `RECORD`, `NUMERIC`, and `WIDE_TEXT` still mostly route through `ContextModelGeneric`; RECORD mode 0 only supplies a fixed stride.
- The implemented detector estimates gain with a low-order byte proxy, not the actual PAQ ensemble. This explains the observed `x-ray` false positive: a stride-2 byte-lane transform improved H0/H1 proxy cost while destroying relationships already exploited by Match, Record, LinearPrediction, Sparse, and other Generic models.
- `sao` improved only slightly because fixed stride 28 reduces RecordModel warm-up, while the original RecordModel already learns much of that periodicity. The patch does not split semantic fields or assign field-specific transforms/backends as an OpenZL-style design would.
- Required architectural consequence: specialist selection must be target-aware, conservative, and capable of abstaining; relabeling a block while keeping the same backend cannot produce domain-codec-level gains.

## External Sources

- OpenZL graph model and universal decode:
  - https://arxiv.org/abs/2510.03203
  - https://openzl.org/getting-started/concepts/
  - OpenZL represents compression as a DAG of reversible codecs. The resolved graph is carried in the frame; standard components can be decoded by a universal decoder.
- OpenZL SAO example and typed splitting:
  - https://engineering.fb.com/2025/10/06/developer-tools/openzl-open-source-format-aware-compression-framework/
  - https://openzl.org/sddl/getting-started/
  - The published SAO pipeline separates the header, converts an array of 28-byte star records into field streams, and compresses homogeneous typed fields. Published complete-file size is 3,516,649 bytes. This is categorically stronger than merely supplying stride 28 to a generic model.
- OpenZL training and exact runtime choice:
  - https://openzl.org/getting-started/quick-start/
  - https://openzl.org/getting-started/examples/c/custom-formats/
  - Training tries compression strategies on parsed streams; OpenZL also exposes dynamic selectors and StoreOnExpansion. This supports an offline oracle/training stage plus a runtime anti-expansion fallback.
- Neural lossless text compression:
  - https://bellard.org/nncp/nncp.pdf
  - https://bellard.org/nncp/nncp_v2.pdf
  - https://bellard.org/nncp/
  - NNCP predicts the next-symbol distribution and arithmetic-codes the observed symbol. Online training can be replayed symmetrically, avoiding transmission of final weights, but compute and deterministic numerical semantics are material constraints.
- PAQ/CMIX neural aggregation precedent:
  - https://mattmahoney.net/dc/dce.html
  - https://github.com/byronknoll/cmix
  - PAQ-style logistic mixing naturally accepts another probability expert. CMIX combines LSTM and hand-built PAQ/PPMD/match models; this is the closest engineering precedent for adding a neural expert without creating a second entropy stream.
- Fair accounting for neural models:
  - https://arxiv.org/pdf/2309.10668
  - Offline pretrained model bytes are part of a two-part code unless the model is a declared external dependency. Reports must separate payload-only and payload-plus-model/program costs.
- Typed numeric transforms:
  - https://parquet.apache.org/docs/file-format/data-pages/encodings/
  - https://blosc.org/pages/
  - Production numeric formats use typed delta/frame-of-reference, dictionary/RLE/bit packing, byte-stream split, shuffle, and bitshuffle; element width and semantic type are essential inputs.
- Images:
  - https://developers.google.com/speed/webp/docs/webp_lossless_bitstream_specification
  - https://jpeg.org/jpegxl/index.html
  - https://github.com/libjxl/libjxl
  - WebP lossless demonstrates reversible spatial/color transforms before entropy coding. JPEG XL explicitly supports lossless pixel coding and exact legacy-JPEG bitstream reconstruction; the latter is a viable file-byte-lossless subcodec.
- Video and audio:
  - https://www.rfc-editor.org/rfc/rfc9043.html
  - https://www.rfc-editor.org/rfc/rfc9639.html
  - FFV1 supplies lossless spatial prediction, contexts, slices, and integrity tools for raw frames. FLAC supplies channel decorrelation, fixed/LPC prediction, and residual coding for PCM. Neither guarantees reconstruction of an arbitrary original media container or an already-compressed source bitstream.
- TAR and self-describing PAQ-family framing:
  - https://www.gnu.org/software/tar/manual/html_section/Standard.html
  - https://mattmahoney.net/dc/zpaq_compression.pdf
  - TAR is a sequence of 512-byte headers, member payloads, padding, and terminators; it should be parsed as a container and recursively routed. ZPAQ provides a PAQ-family precedent for independent, self-describing blocks and embedding the decompression recipe in a block header.

## Synthesis

- The correct target is a hierarchical mixture-of-experts / meta-codec: parser and segmenter -> reversible transform graph -> PAQ model/backend or external subcodec -> versioned independent frame.
- Specialization has three distinct integration levels and must not be conflated:
  1. metadata/model hints while preserving byte order;
  2. reversible transforms feeding PAQ;
  3. independently framed external codec payloads.
- External media codecs are not automatically file-byte-lossless. Raw PCM/pixels can be reconstructed when the original container is explicitly preserved; already-compressed media generally cannot be decode/re-encoded and recover the original bytes. JPEG XL legacy-JPEG reconstruction is a documented exception.
- The selection objective must be actual total bytes, including metadata, model/recipe data, and payload. A low-order entropy proxy cannot provide a no-regression guarantee.
- A new top-level frame format is preferable to overloading the current one-byte `BlockType` plus 32-bit `blockInfo`. Independent frames enable exact candidate trials, direct storage of external bitstreams, parallelism, bounded memory, and decoder limits.
- For text, the first neural path should be a deterministic byte-level probability expert mixed into the existing PAQ Text/Generic mixer. A full token Transformer/NNCP subcodec is a later optional profile because of model dependency, runtime, tokenizer, and reproducibility costs.

## Final Design Decisions

- The final artifact type is a union of exact legacy v216 bytes and the new ExpertGraph format. `MAX_SAFE` compares fully finalized artifacts and commits the legacy bytes unchanged when they win.
- ExpertGraph v1 has fixed 64-byte archive, 108-byte file, and 64-byte stream prefixes; every length is explicit, every entry has an ID/name/content checksum, and normal EOF must equal `archive_length`.
- Recipe direction is unambiguous: original root stream -> forward DAG -> leaves for encoding; decoded leaves -> inverse nodes in reverse topological order -> root for decoding. Each leaf has exactly one payload bound by `leaf_stream_id`.
- V1 always stores a complete canonical Recipe. `profile_id` is a non-semantic routing/audit hint and cannot replace the Recipe.
- `SPLIT_FIXED_RECORDS` explicitly defines header, field-stream, and optional non-empty tail outputs. For the local Silesia SAO file, FULL and PREFIX acceptance are distinct; 32 KiB is `28 + 1169*28 + 8`.
- Numeric/raster transforms use fixed-width unsigned modular arithmetic, explicit endianness, exact multi-channel boundary rules, and a separate graph node for byte shuffle.
- The byte-LM path fixes integer raw-mass normalization, byte-to-bit probability aggregation, update order, canonical tensor bytes, full golden traces, and reproducible wire/standalone/amortized accounting.
- Three independent read-only reviews found no remaining blocker in the wire/selection proof, SAO graph, or neural-text contract after the final corrections.
- No source files were changed and no compilation, codec execution, or compression experiment was performed in this design pass.
