# Exact 1 KiB lossless verification

## Result

PASS. The decompressed file is byte-for-byte identical to the 1024-byte source.

## Evidence

- Toolchain: CMake + Ninja + MinGW GCC/G++ 16.1.0, Release, C++17.
- Build: 145/145 steps completed; `paq8pxsd.exe` linked successfully.
- Compression command: `paq8pxsd.exe -1 input-1024.txt input-1024.paq8pxsd217`.
- Compression exit code: 0.
- Decompression command: `paq8pxsd.exe -d input-1024.paq8pxsd217 restored-1024.txt`.
- Decompression exit code: 0.
- Source length: 1024 bytes.
- Restored length: 1024 bytes.
- Archive length: 63 bytes.
- Source SHA-256: `C7C9D69EC4EB21A5BAC1E72F4B92B83318FC101F2DD08CFB70D070B84893438B`.
- Restored SHA-256: `C7C9D69EC4EB21A5BAC1E72F4B92B83318FC101F2DD08CFB70D070B84893438B`.
- Exact positional comparison: no differing byte; first differing offset `-1`.

## Scope limit

The structured classifier activates only at 16 KiB. This 1 KiB case verifies the derived executable's ordinary single-file archive round trip, but it does not exercise RECORD, NUMERIC, or WIDE_TEXT selection/transforms and is not a compression-gain benchmark.
