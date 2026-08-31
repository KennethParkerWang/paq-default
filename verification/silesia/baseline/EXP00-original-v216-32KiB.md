# EXP00 original PAQ8px v216 — Silesia center 32 KiB baseline

> Correction: local provenance files prove that these values were produced from
> centered 32 KiB slices, not offsets 0..32,767. The screenshot is retained as
> supplied, but it is not a valid baseline for a leading-prefix comparison.

## Provenance

- User-provided reference image: `EXP00 Original PAQ8px v216 (level -1)`.
- Dataset: Silesia.
- Variant: `-1`.
- Repetitions: 1.
- Input for this table: a centered 32 KiB (32,768-byte) slice of every listed file.
- Saved screenshot: `EXP00-original-paq8px-v216-level-minus1.png`.
- Screenshot SHA-256: `E397978DA75E78BBE87D5A48785B3CB282E7B9BA159C3795DAB67F367575BDD2`.
- The screenshot also contains 64 KiB and 128 KiB tabs, but only the visible 32 KiB tab is transcribed here.

## Visible 32 KiB results

| File | Raw bytes | Compressed bytes | bpb | Compress s | Decompress s | RAM MiB | Verify |
|---|---:|---:|---:|---:|---:|---:|---|
| dickens | 32,768 | 9,538 | 2.3286 | 4.397 | 4.553 | 372.0 | PASS |
| mozilla | 32,768 | 11,181 | 2.7297 | 7.003 | 7.244 | 553.3 | PASS |
| mr | 32,768 | 7,608 | 1.8574 | 6.116 | 6.003 | 549.8 | PASS |
| nci | 32,768 | 1,385 | 0.3381 | 3.014 | 2.874 | 365.9 | PASS |
| ooffice | 32,768 | 10,715 | 2.6160 | 6.949 | 6.645 | 553.1 | PASS |
| osdb | 32,768 | 11,369 | 2.7756 | 6.326 | 6.239 | 551.7 | PASS |
| reymont | 32,768 | 5,913 | 1.4436 | 3.455 | 3.415 | 371.8 | PASS |
| samba | 32,768 | 622 | 0.1519 | 3.257 | 3.152 | 369.0 | PASS |
| sao | 32,768 | 19,083 | 4.6589 | 5.848 | 6.367 | 551.6 | PASS |
| webster | 32,768 | 7,063 | 1.7244 | 3.906 | 3.782 | 372.1 | PASS |
| x-ray | 32,768 | 15,022 | 3.6675 | 7.298 | 7.120 | 550.2 | PASS |
| xml | 32,768 | 1,035 | 0.2527 | 3.720 | 4.176 | 362.9 | PASS |

Derived from the visible rows: 393,216 total raw bytes, 100,534 total compressed bytes, and 2.045369 weighted bpb. The screenshot's top-level `1.881 bpb` summary covers all 36 rows across the 32/64/128 KiB result tabs and must not be used as the 32 KiB-only aggregate.

## Comparison role

Treat these values as the immutable EXP00 upstream-v216 center-slice baseline. A valid comparison must use the exact offsets and hashes in `F:\paq8px\benchmark_paq8px_32KiB_parallel\manifest.csv`, level `-1`, one repetition, and the same bpb definition. Leading-prefix experiments must rerun v216 on offsets 0..32,767 instead of using this table.
