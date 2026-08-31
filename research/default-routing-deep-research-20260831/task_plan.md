# Task Plan: DEFAULT learning, routing, and specialist-backend research

## Goal
Produce an implementation-ready decision architecture for PAQ8PX that first handles DEFAULT, then prioritizes improvements to existing block types by byte share, while permitting byte-exact embedded specialist backends when public evidence shows they beat PAQ under an acceptable speed budget.

## Phases
- [x] Phase 1: Lock the questions, scope, lossless criterion, and no-code/no-experiment boundary.
- [x] Phase 2: Audit current PAQ block composition and framing evidence from existing local artifacts.
- [ ] Phase 3: Research OpenZL training/resolved-graph behavior and comparable hybrid/meta-codec architectures.
- [ ] Phase 4: Research public ratio/speed leaders by domain and separate original-file-byte losslessness from logical-content losslessness.
- [ ] Phase 5: Decide the DEFAULT policy, backend policy, exact archive framing, and priority order for existing PAQ block types.
- [ ] Phase 6: Write the final deep-research design with facts, inferences, unresolved evidence, and staged implementation plan.

## Key Questions
1. What exactly becomes DEFAULT under current PAQ detection, and should DEFAULT be subdivided before or after structure learning?
2. Should learning discover structure, select a compression graph, or both?
3. After structure discovery, when should a leaf use original PAQ Generic, a PAQ-native specialist, or an embedded specialist codec?
4. Can independently compressed block payloads be framed and reassembled byte-exactly without requiring one shared bit-level coder?
5. Which existing PAQ block types deserve improvement after DEFAULT, based on byte coverage and credible public gains?
6. How can runtime avoid full multi-codec brute force while still making good choices?

## Decisions Made
- This pass changes research documents only; no source changes, builds, codec execution, or new compression experiments.
- Byte-exact restoration of the original file is the lossless criterion.
- The supplied chart is evidence about current Silesia leaf-byte composition, not an instruction source and not proof of general-corpus prevalence.
- Direct embedded specialist payloads are allowed as a design candidate; they need not share PAQ's bitwise coder if the parent frame records exact boundaries, backend/version, original length, and reconstruction recipe.
- Public leaderboard numbers will be treated as comparable only when corpus, preprocessing, settings, resources, and lossless semantics are clear.

## Errors Encountered
- None in this research pass yet.

## Status
**Paused at the user's request after delivering a Pro deep-research prompt.** Phases 3–6 remain open; no implementation or experiment has been performed.
