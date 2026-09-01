# Notes: EXP02 routed-v2 Silesia leading-prefix benchmark

## Reused immutable evidence
- EXP01 fixture directory: `..\EXP01-paq8pxsd-v217-32KiB\inputs`.
- Each fixture is exactly 32,768 bytes and was previously proven to equal source offset 0 through 32,767.
- Original v216 baseline executable and its expected SHA-256 are unchanged from EXP01.
- EXP01 original v216 total on these fixtures was 97,555 bytes; EXP02 will rerun it rather than import those numbers.

## Candidate source
- Repository: `F:\paq8px\paq8px-structured-default-v2-20260831`.
- Git commit: `77b2c328838df32ffe9feafd88adf1363184cbb7`.
- Required Windows compile fix: internal enum token `OPAQUE` renamed to `OPAQUE_DATA`; its numeric value and every routing/serialization decision remain unchanged.
- Branch at start: `main`, synchronized with `origin/main`.
- Canonical expert build: CMake Release, `ENABLE_OPENZL=ON`.

## Interpretation rule
- Actual on-disk bytes are primary; no header normalization will replace them.
- A compression result is reportable only after the corresponding decoded file matches length, every byte, and SHA-256.
- One un-warmed timing repetition is descriptive only, not a stable speed claim.

## Harness and dependency status
- `run-benchmark.ps1` passed PowerShell AST parsing without executing either codec.
- The approved OpenZL commit was obtained with a partial/sparse Git checkout because two full-history transfers stalled; this preserves the exact pinned commit and clean-worktree provenance checks.
- OpenZL gitlinks are fixed at zstd `f8745da6ff1ad1e7bab384bd1f9d742439278e99` and lz4 `ebb370ca83af193212df4dcbadcc5d87bc0de2f0`.
- Candidate executable: `verification/build/routed-v219-77b2c32-fresh/paq8pxhy.exe`, 6,400,512 bytes, SHA-256 `08E24EA1B331B25A834BDD15EC96FEE8E6696291D1E492816FCFEF11E129C7E9`.

## Completed result
- Original total: 97,555 bytes, 1.984762 bpb.
- Candidate total: 102,060 bytes, 2.076416 bpb.
- Delta: +4,505 bytes, +4.617908%, +0.091654 bpb.
- Reconstruction: original 12/12 and candidate 12/12 passed length, SHA-256, and independent byte comparison.
- Candidate routing: 20 segments = 20 PAQ + 0 external. This prefix benchmark therefore measures routed-v2/PAQ framing, not an external expert's compression benefit.
- Independent audit also confirmed each fixture is exactly source offset 0 through 32,767, all archive hashes/sizes, the 12 routed magics, and the 24 archive / 24 restored / 96 log artifact counts.
- The child-process peak-working-set counters were all zero and are excluded from RAM conclusions; codec-reported memory values remain valid.
