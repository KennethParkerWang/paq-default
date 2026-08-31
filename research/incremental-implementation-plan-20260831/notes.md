# Notes: why the order matters

- The current archive uses one arithmetic stream and the existing block header has no independent `payload_len`.
- The current structured prototype adds about 2,038 source lines versus the original v216 tree; 1,879 lines are concentrated in `DefaultStructureDetector.hpp` and `StructuredDataFilter.hpp`.
- Existing reversible primitives are reusable, but the current `RECORD/NUMERIC/WIDE_TEXT -> BlockType` binding is not the target hybrid abstraction.
- The highest-risk common dependency is the new outer frame and PAQ-segment boundary. Testing experts before this layer would mix framing bugs with modeling results.
- A small round trip proves local correctness only. A targeted complete file proves actual reconstruction and metadata amortization for that file. Independent files are still required for generalization.
