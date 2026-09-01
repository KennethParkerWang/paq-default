#pragma once

#include "../BlockType.hpp"
#include "../Utils.hpp"
#include "../hybrid/HybridFormat.hpp"
#include "../hybrid/ProfileParameters.hpp"
#include "../hybrid/ProfileRegistry.hpp"
#include "StructuredDataFilter.hpp"

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
  // The number of source bytes this span must restore exactly.
  uint64_t sourceLength = 0;
  // Stage 3's semantic hint; it still selects the unchanged PAQ block model.
  BlockType legacyType = BlockType::DEFAULT;
  int blockInfo = 0;
  StructureProfileKind structureProfile = StructureProfileKind::NONE;
  routed::RouteDecision profileDecision = routed::ProfileRegistry::fallback();
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

inline routed::RouteDecision routeDecisionForLegacyType(BlockType type,
                                                         int blockInfo) {
  routed::RouteDecision decision = routed::ProfileRegistry::fallback();
  const auto setProfile = [&decision](routed::ProfileId profile,
                                      routed::ExpertId expert,
                                      routed::EvidenceKind evidence) {
    decision.pipeline = {profile, 1, 0};
    decision.evidence = evidence;
    decision.statePolicy = routed::StatePolicy::CONTINUE_LOCAL;
    decision.expert = expert;
    decision.transform = routed::TransformId::NONE;
    decision.reasonCode = 0x80000000u | static_cast<uint32_t>(profile);
  };

  if (type == BlockType::TEXT || type == BlockType::TEXT_EOL) {
    setProfile(routed::ProfileId::TEXT_UTF8, routed::ExpertId::PAQ_TEXT,
               routed::EvidenceKind::STRICT_CONTENT);
  }
  else if (type == BlockType::DBF) {
    setProfile(routed::ProfileId::TEXT_ROWS, routed::ExpertId::PAQ_ROWS,
               routed::EvidenceKind::EXACT_FORMAT);
  }
  else if (type == BlockType::WIDE_TEXT) {
    setProfile(routed::ProfileId::TEXT_WIDE, routed::ExpertId::PAQ_WIDE_TEXT,
               routed::EvidenceKind::STRICT_CONTENT);
  }
  else if (type == BlockType::RECORD) {
    setProfile(routed::ProfileId::RECORD_STRIDE_CTX,
               routed::ExpertId::PAQ_RECORD,
               routed::EvidenceKind::TRUSTED_DESCRIPTOR);
  }
  else if (type == BlockType::IMAGE1 || type == BlockType::IMAGE4 ||
           type == BlockType::IMAGE8 || type == BlockType::IMAGE8GRAY ||
           type == BlockType::IMAGE24 || type == BlockType::IMAGE32 ||
           type == BlockType::PNG8 || type == BlockType::PNG8GRAY ||
           type == BlockType::PNG24 || type == BlockType::PNG32) {
    setProfile(routed::ProfileId::RASTER_IMAGE, routed::ExpertId::PAQ_IMAGE,
               routed::EvidenceKind::EXACT_FORMAT);
  }
  else if (type == BlockType::AUDIO || type == BlockType::AUDIO_LE) {
    setProfile(routed::ProfileId::PCM_AUDIO, routed::ExpertId::PAQ_AUDIO,
               routed::EvidenceKind::EXACT_FORMAT);
  }
  else if (type == BlockType::EXE) {
    setProfile(routed::ProfileId::MACHINE_CODE,
               routed::ExpertId::PAQ_MACHINE_CODE,
               routed::EvidenceKind::EXACT_FORMAT);
  }

  if (decision.pipeline.profileId == routed::ProfileId::TEXT_UTF8 ||
      decision.pipeline.profileId == routed::ProfileId::TEXT_ROWS) {
    routed::TextParams parameters;
    parameters.flavor = decision.pipeline.profileId == routed::ProfileId::TEXT_ROWS
      ? routed::TextFlavor::STRUCTURED : routed::TextFlavor::GENERIC;
    if (decision.pipeline.profileId == routed::ProfileId::TEXT_ROWS && blockInfo > 0)
      parameters.fixedColumnSchemaId = static_cast<uint32_t>(blockInfo);
    decision.parameters = routed::encodeTextParams(parameters);
  }
  else if (decision.pipeline.profileId == routed::ProfileId::RECORD_STRIDE_CTX &&
           structured::isValidRecordInfo(static_cast<uint32_t>(blockInfo))) {
    routed::RecordParams parameters;
    parameters.stride = structured::unpackRecordStride(
      static_cast<uint32_t>(blockInfo));
    decision.parameters = routed::encodeRecordParams(parameters);
  }
  else if (decision.pipeline.profileId != routed::ProfileId::PAQ_DEFAULT) {
    // Existing exact PAQ format adapters retain their already frozen blockInfo
    // layout until a profile-specific parameter adapter replaces it.
    const uint32_t value = static_cast<uint32_t>(blockInfo);
    decision.parameters = {
      static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
      static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)
    };
  }
  return decision;
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
    append(sourceOffset, sourceLength, legacyType, blockInfo,
           routeDecisionForLegacyType(legacyType, blockInfo), route);
  }

  void append(uint64_t sourceOffset, uint64_t sourceLength, BlockType legacyType,
              int blockInfo, const routed::RouteDecision& profileDecision,
              hybrid::RouteId route = hybrid::RouteId::PAQ_LEGACY_ARCHIVE) {
    if (sealed_)
      quit("Cannot append to a sealed block plan.");

    PlannedBlock block;
    block.sourceOffset = sourceOffset;
    block.sourceLength = sourceLength;
    block.legacyType = legacyType;
    block.blockInfo = blockInfo;
    block.structureProfile = structureProfileFor(legacyType);
    block.profileDecision = profileDecision;
    block.route = route;
    validateBlockMetadata(block);

    const uint64_t expectedOffset = nextExpectedOffset();
    if (sourceOffset != expectedOffset)
      quit("Block plan is not contiguous.");
    if (sourceOffset > std::numeric_limits<uint64_t>::max() - sourceLength)
      quit("Block plan range overflow.");
    if (sourceOffset + sourceLength > sourceEnd())
      quit("Block plan exceeds its source range.");
    blocks_.push_back(block);
  }

  void seal() {
    validateCoverage();
    sealed_ = true;
  }

  // This validator is intentionally independent of detection and encoding.
  // It may be called on a fully constructed unsealed plan, and encoding calls
  // it again after sealing so coverage cannot depend only on append-time state.
  void validateCoverage() const {
    uint64_t expectedOffset = sourceBegin_;
    for (const PlannedBlock& block : blocks_) {
      validateBlockMetadata(block);
      if (block.sourceOffset != expectedOffset)
        quit("Block plan spans are unordered, overlapping, or contain a gap.");
      if (block.sourceOffset >
          std::numeric_limits<uint64_t>::max() - block.sourceLength)
        quit("Block plan range overflow.");
      expectedOffset = block.sourceOffset + block.sourceLength;
      if (expectedOffset > sourceEnd())
        quit("Block plan exceeds its source range.");
    }
    if (expectedOffset != sourceEnd())
      quit("Block plan does not cover the complete source range.");
  }

  void validateForEncoding() const {
    if (!sealed_)
      quit("Cannot encode an unsealed block plan.");
    validateCoverage();
  }

  bool sealed() const { return sealed_; }
  uint64_t sourceBegin() const { return sourceBegin_; }
  uint64_t sourceLength() const { return sourceLength_; }
  uint64_t sourceEnd() const { return sourceBegin_ + sourceLength_; }
  size_t size() const { return blocks_.size(); }
  const PlannedBlock& operator[](size_t index) const { return blocks_[index]; }

private:
  static void validateBlockMetadata(const PlannedBlock& block) {
    if (block.sourceLength == 0)
      quit("Block plan contains an empty block.");
    if (static_cast<unsigned>(block.legacyType) >=
        static_cast<unsigned>(BlockType::Count))
      quit("Block plan contains an invalid legacy block type.");
    if (block.structureProfile != structureProfileFor(block.legacyType))
      quit("Block plan structure profile disagrees with its semantic hint.");
    if (structured::isStructuredType(block.legacyType) &&
        !structured::isValidStructuredInfo(
          block.legacyType, static_cast<uint32_t>(block.blockInfo)))
      quit("Block plan contains invalid structured metadata.");
    if (block.route != hybrid::RouteId::PAQ_LEGACY_ARCHIVE)
      quit("Stage 3 block plans may only select the legacy PAQ route.");

    const routed::RouteDecision& decision = block.profileDecision;
    const routed::ProfileSpec* profile =
      routed::ProfileRegistry::find(decision.pipeline.profileId);
    if (!decision.selected || profile == nullptr ||
        decision.pipeline.profileRevision != profile->revision ||
        !routed::ProfileRegistry::allowsEvidence(*profile, decision.evidence) ||
        !routed::ProfileRegistry::allowsVariant(
          profile->id, decision.pipeline.variantId) ||
        !routed::ProfileRegistry::allowsExpert(profile->id, decision.expert) ||
        !routed::isImplementedExpert(decision.expert) ||
        (routed::isDestructiveTransform(decision.transform) &&
         !routed::evidenceMayUseSchemaTransform(decision.evidence)))
      quit("Block plan contains an invalid routed-profile decision.");
  }

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
