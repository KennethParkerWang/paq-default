#pragma once

#include "../BlockType.hpp"
#include "../Utils.hpp"
#include "../hybrid/HybridFormat.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

enum class StructureProfileKind : uint16_t {
  NONE = 0,
  FIXED_RECORD = 1,
  NUMERIC_ARRAY = 2,
  WIDE_TEXT = 3
};

struct PlannedBlock {
  uint64_t sourceOffset = 0;
  uint64_t sourceLength = 0;
  BlockType legacyType = BlockType::DEFAULT;
  int blockInfo = 0;
  StructureProfileKind structureProfile = StructureProfileKind::NONE;
  hybrid::RouteId route = hybrid::RouteId::PAQ_LEGACY_ARCHIVE;
};

inline StructureProfileKind structureProfileFor(BlockType type) {
  if (type == BlockType::RECORD)
    return StructureProfileKind::FIXED_RECORD;
  if (type == BlockType::NUMERIC)
    return StructureProfileKind::NUMERIC_ARRAY;
  if (type == BlockType::WIDE_TEXT)
    return StructureProfileKind::WIDE_TEXT;
  return StructureProfileKind::NONE;
}

// A plan is encoder-only metadata. It does not alter the legacy PAQ bitstream:
// Stage 3 records every selected span first, verifies exact coverage, and only
// then calls the existing block encoder in the same order.
class BlockPlan {
public:
  BlockPlan(uint64_t sourceBegin, uint64_t sourceLength)
    : sourceBegin_(sourceBegin), sourceLength_(sourceLength), sealed_(false) {
    if (sourceBegin_ > std::numeric_limits<uint64_t>::max() - sourceLength_)
      quit("Block plan source range overflow.");
  }

  void append(uint64_t sourceOffset, uint64_t sourceLength, BlockType legacyType,
              int blockInfo,
              hybrid::RouteId route = hybrid::RouteId::PAQ_LEGACY_ARCHIVE) {
    if (sealed_)
      quit("Cannot append to a sealed block plan.");
    if (sourceLength == 0)
      quit("Block plan contains an empty block.");
    if (static_cast<unsigned>(legacyType) >= static_cast<unsigned>(BlockType::Count))
      quit("Block plan contains an invalid legacy block type.");
    if (route != hybrid::RouteId::PAQ_LEGACY_ARCHIVE)
      quit("Stage 3 block plans may only select the legacy PAQ route.");

    const uint64_t expectedOffset = nextExpectedOffset();
    if (sourceOffset != expectedOffset)
      quit("Block plan is not contiguous.");
    if (sourceOffset > std::numeric_limits<uint64_t>::max() - sourceLength)
      quit("Block plan range overflow.");
    if (sourceOffset + sourceLength > sourceEnd())
      quit("Block plan exceeds its source range.");

    PlannedBlock block;
    block.sourceOffset = sourceOffset;
    block.sourceLength = sourceLength;
    block.legacyType = legacyType;
    block.blockInfo = blockInfo;
    block.structureProfile = structureProfileFor(legacyType);
    block.route = route;
    blocks_.push_back(block);
  }

  void seal() {
    if (nextExpectedOffset() != sourceEnd())
      quit("Block plan does not cover the complete source range.");
    sealed_ = true;
  }

  bool sealed() const { return sealed_; }
  uint64_t sourceBegin() const { return sourceBegin_; }
  uint64_t sourceLength() const { return sourceLength_; }
  uint64_t sourceEnd() const { return sourceBegin_ + sourceLength_; }
  size_t size() const { return blocks_.size(); }
  const PlannedBlock& operator[](size_t index) const { return blocks_[index]; }

private:
  uint64_t nextExpectedOffset() const {
    if (blocks_.empty())
      return sourceBegin_;
    const PlannedBlock& last = blocks_.back();
    return last.sourceOffset + last.sourceLength;
  }

  uint64_t sourceBegin_;
  uint64_t sourceLength_;
  std::vector<PlannedBlock> blocks_;
  bool sealed_;
};
