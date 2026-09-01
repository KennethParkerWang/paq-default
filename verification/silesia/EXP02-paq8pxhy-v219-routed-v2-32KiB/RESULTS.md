# EXP02: PAQ8px v216 vs paq8pxhy v219 routed-v2 on Silesia leading 32 KiB

## Status

**COMPLETE — all 24 codec round trips passed exact reconstruction checks, and an independent artifact audit passed.**

## Locked protocol

- Inputs: the exact offset-0 range `[0, 32768)` of `dickens, mozilla, mr, nci, ooffice, osdb, reymont, samba, sao, webster, x-ray, xml`.
- Original: PAQ8px v216, level `-1`, SHA-256 `F79343702F596A4FA6C7CC3E25F2FA9C05199EAF11F06655B065F687E5F42533`.
- Candidate: paq8pxhy v219 routed-v2, Git commit `77b2c328838df32ffe9feafd88adf1363184cbb7` plus the compile-only Windows `OPAQUE_DATA` rename, Release with `ENABLE_OPENZL=ON`; executable SHA-256 `08E24EA1B331B25A834BDD15EC96FEE8E6696291D1E492816FCFEF11E129C7E9`.
- Execution: serial, one repetition, no warmup.
- Primary measurements: actual archive bytes, bpb, candidate-minus-original delta, and exact lossless reconstruction.
- Secondary observations: encode/decode wall time, process peak working set, codec-reported memory, and routed segment counts.
- No header normalization: every comparison uses actual on-disk archive bytes.
- Scope limit: leading 32 KiB results do not establish whole-file behavior and may not exercise an external expert whose exact-format parser needs later bytes or the complete file.

## Conclusion

The candidate is lossless on all 12 leading-prefix fixtures, but it is larger on every file. Total archive size increased from 97,555 to 102,060 bytes: **+4,505 bytes (+4.617908%)**, or 1.984762 to 2.076416 bpb. All 20 candidate segments used the PAQ fallback and none used an external expert, so this experiment validates the routed container/PAQ-fragment path but does not measure OpenZL's full-SAO route.

The size pattern is consistent with routed framing rather than a changed PAQ model: the format stores a 64-byte archive header and, for each PAQ segment, a 128-byte segment header plus a 64-byte canonical PAQ decoder configuration. Predictor state remains continuous across PAQ fragments, while each fragment has its own arithmetic-coder boundary. One-segment files therefore grew by roughly 245–251 bytes; every additional PAQ segment added roughly another 192 bytes.

## Aggregate comparison

| Metric | Original PAQ8px v216 | Candidate paq8pxhy v219 | Change |
|---|---:|---:|---:|
| Total source bytes | 393,216 | 393,216 | 0 |
| Actual archive bytes | 97,555 | 102,060 | +4,505 (+4.617908%) |
| Actual bpb | 1.984762 | 2.076416 | +0.091654 |
| Exact round trips | 12 / 12 | 12 / 12 | — |
| Routed segments | — | 20 | — |
| PAQ segments | — | 20 | — |
| External segments | — | 0 | — |

## Per-file results

| File | Original bytes | Candidate bytes | Delta bytes | Delta % | Original bpb | Candidate bpb | Routed (PAQ / external) | Lossless |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| dickens | 9,502 | 9,748 | +246 | +2.588929% | 2.319824 | 2.379883 | 1 (1 / 0) | PASS |
| mozilla | 5,706 | 6,723 | +1,017 | +17.823344% | 1.393066 | 1.641357 | 5 (5 / 0) | PASS |
| mr | 5,117 | 5,366 | +249 | +4.866132% | 1.249268 | 1.310059 | 1 (1 / 0) | PASS |
| nci | 1,270 | 1,517 | +247 | +19.448819% | 0.310059 | 0.370361 | 1 (1 / 0) | PASS |
| ooffice | 7,218 | 7,848 | +630 | +8.728180% | 1.762207 | 1.916016 | 3 (3 / 0) | PASS |
| osdb | 11,371 | 11,619 | +248 | +2.180987% | 2.776123 | 2.836670 | 1 (1 / 0) | PASS |
| reymont | 5,609 | 5,854 | +245 | +4.367980% | 1.369385 | 1.429199 | 1 (1 / 0) | PASS |
| samba | 7,811 | 8,251 | +440 | +5.633082% | 1.906982 | 2.014404 | 2 (2 / 0) | PASS |
| sao | 18,756 | 19,007 | +251 | +1.338238% | 4.579102 | 4.640381 | 1 (1 / 0) | PASS |
| webster | 9,154 | 9,403 | +249 | +2.720122% | 2.234863 | 2.295654 | 1 (1 / 0) | PASS |
| x-ray | 14,652 | 14,898 | +246 | +1.678952% | 3.577148 | 3.637207 | 1 (1 / 0) | PASS |
| xml | 1,389 | 1,826 | +437 | +31.461483% | 0.339111 | 0.445801 | 2 (2 / 0) | PASS |

## Lossless and format verification

The harness and an independent read-only audit confirmed all of the following:

- 12/12 fixtures still have length 32,768 and their locked SHA-256 values.
- Both codecs exit 0 for all 12 compression and all 12 decompression operations.
- All 24 restored files match source length, every byte, and SHA-256.
- All 12 candidate archives start with the exact eight bytes `PAQXRP2\n`.
- Every candidate compression log contains a consistent `Routed segments: total (PAQ n, external n)` line.
- Artifact counts are 12 original archives, 12 candidate archives, 24 restored files, and 96 separate stdout/stderr logs.

The independently recomputed original total is 97,555 bytes, matching the earlier locked offset-0 baseline. `results.csv` SHA-256 is `2E33B6F7198A980A9636ACFA634A5D946D52A61A21147BBC673D6E89DF63F6B5`.

## Timing limits

One un-warmed repetition is enough to record elapsed time but not enough to claim a stable speed difference. Descriptively, summed compression time was 50.331 s original versus 108.655 s candidate (2.159x); summed decompression time was 50.042 s versus 107.982 s (2.158x). The process peak-working-set field returned zero for every short-lived child process and is invalid; codec-reported memory remains in `results.csv` and showed no material increase. No stable speed or OS peak-RAM conclusion is claimed.

## Reproduction

Run `run-benchmark.ps1` with the absolute candidate executable path and its already-recorded SHA-256:

```powershell
.\run-benchmark.ps1 `
  -CandidateCodecPath 'F:\absolute\path\to\paq8pxhy.exe' `
  -ExpectedCandidateSha256 '<64-hex-SHA256>'
```

The harness refuses to overwrite any existing archive, restored file, log, or `results.csv`.
