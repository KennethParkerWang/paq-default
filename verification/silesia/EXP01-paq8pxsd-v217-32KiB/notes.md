# Notes: EXP01 inputs and observations

## Locked Inputs

- Corpus root: `F:\paq8px\silesia`.
- All 12 required source files exist and exceed 32,768 bytes.
- Derived executable: `F:\paq8px\paq8px-structured-default-v2-20260831\verification\build\paq8pxsd.exe`.
- Executable size: 1,472,512 bytes.
- Executable SHA-256: `4BAE2C77D1C9BA8EB4B6F2E435DE140E3BA9BE26C8D9CE4FD0662ED09BF5C8D5`.
- EXP00 screenshot SHA-256: `E397978DA75E78BBE87D5A48785B3CB282E7B9BA159C3795DAB67F367575BDD2`.
- Original v216 executable: `F:\paq8px\PaqBenchStudio\staging-v1.1.0\paq8px.exe`.
- Original executable size: 1,383,936 bytes.
- Original executable SHA-256: `F79343702F596A4FA6C7CC3E25F2FA9C05199EAF11F06655B065F687E5F42533`, matching the screenshot's visible configuration.

## Corpus File Sizes

| File | Full source bytes |
|---|---:|
| dickens | 10,192,446 |
| mozilla | 51,220,480 |
| mr | 9,970,564 |
| nci | 33,553,445 |
| ooffice | 6,152,192 |
| osdb | 10,085,684 |
| reymont | 6,627,202 |
| samba | 21,606,400 |
| sao | 7,251,944 |
| webster | 41,458,703 |
| x-ray | 8,474,240 |
| xml | 5,345,280 |

## Observations

- All 12 fixtures are exactly 32,768 bytes and were compared byte-by-byte against offsets 0..32,767 of their source files; all comparisons passed.
- The screenshot values match an earlier local center-slice run exactly. Its manifest uses nonzero offsets (for example dickens offset 5,079,839), so those values cannot be compared with these leading-prefix fixtures.
- Fairness decision: rerun the exact screenshot-matching original v216 executable on these fixtures, then run the derived executable on the same fixtures.

## Prefix SHA-256

| File | SHA-256 |
|---|---|
| dickens | `FC42DCB9849222C8704C9DCAE606D075B389B66244FB215035148D6409EC0B31` |
| mozilla | `9DDEEF36CA0CA55B72FE3376D005926DFF3400A2ADE6EAE18482D8017D8645DB` |
| mr | `3BB287B0AF65F777AB00C14E362B4D1962087260556001BBEE0689AA10D9F76A` |
| nci | `0D3034FE8B0E573DE1439ED98CE409B83B13E5895085C9BFB0BF980F4962FB79` |
| ooffice | `2ACF9B4CAAEAC5814CDFEF0FA48A5ECE857C847DBDB7B44EEAB95CA3C098921C` |
| osdb | `03B2C20B777682CD960BCD893D7A4463161D9C0200FD1708B2EFAE32A259D0B7` |
| reymont | `9D5E4B9340C2260DAE7DDB01B3EDA58236FF2DE5E1FF4D56767AA840B6FB1A87` |
| samba | `9C4F5BEE544E4531E4946E62F796987418B4526911C34877EF282F14DD57AD12` |
| sao | `95677430CAD4F000506BC5EE22815C3F4E13D64B477ED506B78D5D9ACFB50CCD` |
| webster | `774D224695AF4057353F72602BB96CD2AFEEA059AC7737535645F11596FCA85E` |
| x-ray | `10511AA63DFBD9C0DDBFEFE36068740103D4BA1116E214154A0772057D7E9314` |
| xml | `C2F7F129956F8D6FC3D0E3595D93F98D5270B6F5BC4AB5E70F45867822EFDD71` |

## Run Results

- All 24 round trips (12 files × original/derived codecs) completed; all 24 restored files equal their fixture SHA-256 and exact bytes.
- Total input: 393,216 bytes.
- Original v216 archives: 97,555 bytes, 1.984762 bpb.
- Derived v217 archives: 98,174 bytes, 1.997355 bpb.
- Raw change: +619 bytes, +0.012594 bpb, +0.634514% (worse).
- The derived magic is two bytes longer per archive. After subtracting 24 bytes across 12 files: 98,150 normalized bytes, 1.996867 bpb, +595 bytes / +0.609912% versus original.
- `sao` selected RECORD stride 28, mode 0 and improved by 20 raw archive bytes (22 bytes after header normalization).
- `x-ray` selected RECORD stride 2, mode 2 and regressed by 622 raw archive bytes (620 bytes after normalization).
- No NUMERIC or WIDE_TEXT block was selected. All other files retained the original block-type segmentation.
- Excluding `x-ray`, the raw aggregate delta is -3 bytes; its false-positive RECORD selection dominates the overall regression.
- Single-run wall times are retained in `results.csv` but are not treated as stable performance claims.
