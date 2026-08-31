# Notes: DEFAULT learning, routing, and specialist backends

## Scope

- First priority: DEFAULT handling.
- Second priority: improve existing PAQ block classes according to observed/common byte coverage.
- Evaluate three leaf backend families: original PAQ Generic, PAQ-native specialist mechanism, and embedded byte-exact specialist codec.
- No code or experimental runs in this pass.

## Evidence Log

### Existing Silesia leaf-byte accounting

- Source: `F:/paq8px/type-block-stats-20260806/summary_by_file_type.csv`; 12 files, total 211,938,580 original bytes, leaf coverage approximately 100% for every file.
- Aggregate leaf-byte shares:
  - TEXT: 69,085,882 B, 32.60%.
  - TEXT_EOL: 55,268,119 B, 26.08%.
  - DEFAULT: 54,826,499 B, 25.87%.
  - DEC_ALPHA: 24,302,653 B, 11.47%.
  - EXE: 5,266,002 B, 2.48%.
  - All other individual leaf types are below 1% on this corpus.
- If TEXT and TEXT_EOL are treated as one family, text covers 58.68%, so the supplied chart does not support a corpus-wide claim that DEFAULT is the largest family. It does support the claim that DEFAULT is a top-priority single route and dominates several important files.
- DEFAULT distribution is heterogeneous:
  - mozilla: 17,967,722 B / 35.08% of that file.
  - osdb: 10,085,684 B / 100%.
  - mr: 9,970,564 B / 100%.
  - x-ray: 8,474,240 B / 100%.
  - sao: 7,251,944 B / 100%.
  - ooffice: 886,190 B / 14.40%.
  - samba and reymont contain small residual DEFAULT regions.
- Consequence: DEFAULT is not a semantic class. It is the complement of the current detector set and includes unrelated structured records, raster-like data, database-like data, container residuals, and genuinely generic bytes.

### Existing archive framing

- `src/Block.cpp` writes a block type, four-byte block size, and optional four-byte info before initializing the selected model.
- `src/filter/Filters.hpp::decompressRecursive()` reads block frames sequentially, restores each block, and writes restored bytes in original order until the enclosing original byte count is reached.
- Therefore independent leaf backends are architecturally possible. They require a new parent/backend frame carrying restored length, payload length, backend/version, parameters, and exact reconstruction metadata. They do not need to share PAQ's bitwise probability stream.
- Current structured transforms are length-preserving and fit the existing simple header. Multi-stream or foreign-codec payloads require an explicit extended frame rather than bare concatenation.

## Synthesis

- Local evidence rejects a simplistic global order of DEFAULT > TEXT. On Silesia, text-family bytes are much larger in aggregate, while DEFAULT is the most heterogeneous high-priority route.
- Priority must combine byte coverage, achievable gain, implementation risk, and applicability beyond Silesia.
