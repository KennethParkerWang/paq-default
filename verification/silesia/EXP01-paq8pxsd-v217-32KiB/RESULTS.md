# EXP01: PAQ8px v216 vs paq8pxsd v217 on Silesia leading 32 KiB

## Conclusion

All 12 files were lossless with both codecs. The derived build is worse on this leading-prefix set: its actual archives total 98,174 bytes versus 97,555 bytes for original v216, a regression of 619 bytes (`+0.634514%`) or `+0.012594 bpb`.

The result is dominated by `x-ray`: the new classifier selected RECORD stride 2 / mode 2 and added 622 bytes. RECORD stride 28 / mode 0 improved `sao` by 20 bytes. No NUMERIC or WIDE_TEXT block was selected.

## Aggregate comparison

| Metric | Original PAQ8px v216 | Derived paq8pxsd v217 | Change |
|---|---:|---:|---:|
| Total source bytes | 393,216 | 393,216 | 0 |
| Actual archive bytes | 97,555 | 98,174 | +619 (+0.634514%) |
| Actual bpb | 1.984762 | 1.997355 | +0.012594 |
| Header-normalized archive bytes | 97,555 | 98,150 | +595 (+0.609912%) |
| Header-normalized bpb | 1.984762 | 1.996867 | +0.012105 |
| Exact round trips | 12/12 PASS | 12/12 PASS | — |

`paq8pxsd` deliberately uses an 8-byte magic while original `paq8px` uses 6 bytes. The normalized row subtracts exactly 2 bytes from every derived archive (24 bytes total); the actual archive row remains the primary on-disk result.

## Per-file results

| File | Original bytes | Derived bytes | Raw delta | Raw delta % | Original bpb | Derived bpb | Normalized delta | Block-type change | Lossless |
|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| dickens | 9,502 | 9,502 | 0 | 0.0000% | 2.319824 | 2.319824 | -2 | text-eol → text-eol | PASS |
| mozilla | 5,706 | 5,708 | +2 | +0.0351% | 1.393066 | 1.393555 | 0 | unchanged mixed segmentation | PASS |
| mr | 5,117 | 5,120 | +3 | +0.0586% | 1.249268 | 1.250000 | +1 | DEFAULT → DEFAULT | PASS |
| nci | 1,270 | 1,271 | +1 | +0.0787% | 0.310059 | 0.310303 | -1 | TEXT → TEXT | PASS |
| ooffice | 7,218 | 7,219 | +1 | +0.0139% | 1.762207 | 1.762451 | -1 | unchanged DEFAULT/x86-64 segmentation | PASS |
| osdb | 11,371 | 11,373 | +2 | +0.0176% | 2.776123 | 2.776611 | 0 | DEFAULT → DEFAULT | PASS |
| reymont | 5,609 | 5,608 | -1 | -0.0178% | 1.369385 | 1.369141 | -3 | TEXT → TEXT | PASS |
| samba | 7,811 | 7,813 | +2 | +0.0256% | 1.906982 | 1.907471 | 0 | unchanged TAR/DEFAULT segmentation | PASS |
| sao | 18,756 | 18,736 | -20 | -0.1066% | 4.579102 | 4.574219 | -22 | DEFAULT → RECORD stride 28, mode 0 | PASS |
| webster | 9,154 | 9,159 | +5 | +0.0546% | 2.234863 | 2.236084 | +3 | text-eol → text-eol | PASS |
| x-ray | 14,652 | 15,274 | +622 | +4.2452% | 3.577148 | 3.729004 | +620 | DEFAULT → RECORD stride 2, mode 2 | PASS |
| xml | 1,389 | 1,391 | +2 | +0.1440% | 0.339111 | 0.339600 | 0 | unchanged DEFAULT/TEXT segmentation | PASS |

Without `x-ray`, the other 11 files have a combined raw change of -3 bytes. This does not justify excluding `x-ray`; it shows that one false-positive classification determines the aggregate regression.

## Verification evidence

- Every fixture is exactly 32,768 bytes and was independently compared with offsets 0..32,767 of the corresponding Silesia source.
- Both codecs compressed and decompressed every fixture with exit code 0.
- The harness compared lengths, every byte position, and SHA-256 after each decompression; all 24 restored files passed.
- A second post-run SHA-256 audit independently confirmed all original and derived restored files.
- Artifact counts: 12 original archives, 12 derived archives, 24 restored files, and 48 codec logs.
- Raw data: `results.csv`; reproducible harness: `run-benchmark.ps1`; detailed output: `logs/`.

## Baseline correction

The supplied screenshot was described as the first 32 KiB, but its numbers exactly match `F:\paq8px\benchmark_paq8px_32KiB_parallel`. That experiment's manifest records centered slices with nonzero offsets. Consequently, this experiment reran the screenshot-matching original v216 binary on the true leading prefixes instead of comparing different input bytes.

Original binary SHA-256: `F79343702F596A4FA6C7CC3E25F2FA9C05199EAF11F06655B065F687E5F42533`.

Derived binary SHA-256: `4BAE2C77D1C9BA8EB4B6F2E435DE140E3BA9BE26C8D9CE4FD0662ED09BF5C8D5`.

## Environment and limits

- OS: Windows 10 Pro 10.0.19045.
- CPU: Intel Core i7-12650H, 16 logical processors.
- Physical RAM: 16,905,449,472 bytes.
- Derived build: CMake 4.3.2, Ninja, MinGW GCC/G++ 16.1.0, Release, C++17.
- Runs were serial, one repetition, without warmup, matching the requested/screenshot repetition count.
- Measured wall time includes process startup and codec I/O; fixture creation and post-run hashing are excluded.
- Total compression wall time was 47.939 s original and 47.916 s derived; decompression was 47.786 s original and 46.882 s derived. With one un-warmed repetition, these timings are observations, not reliable speed claims.
