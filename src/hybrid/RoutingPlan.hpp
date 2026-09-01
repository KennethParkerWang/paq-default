#pragma once

#include "../Utils.hpp"
#include "ProfileRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace routed {

// A CommitUnit is the smallest source span whose selected route can fall back
// before archive emission.  Version 1 deliberately maps one CommitUnit to one
// outer segment; TransportFrame is retained as an explicit future extension
// point, not as permission to reset or reroute model state.
struct TransportFrame {
  uint64_t commitId = 0;
  uint32_t frameIndex = 0;
  uint64_t sourceOffset = 0;
  uint64_t sourceLength = 0;
  bool finalFrame = true;
};

struct CommitUnit {
  uint64_t commitId = 0;
  uint64_t sourceOffset = 0;
  uint64_t sourceLength = 0;

  // Range in the flattened top-level BlockPlan.  Container recursion must
  // allocate its children into the same flattened index space before seal().
  size_t firstBlockIndex = 0;
  size_t blockCount = 0;

  RouteDecision requestedDecision = ProfileRegistry::fallback();
  RouteDecision decision =
    ProfileRegistry::nativePaqFallback(); // Frozen final route.
  bool routeFinalized = false;

  StateFamily stateFamily = StateFamily::PAQ_MODEL_SESSION_V1;
  std::vector<uint8_t> reconstructionRecipe;
  std::vector<TransportFrame> frames;
};

inline void validateCommittedDecision(const CommitUnit& commit) {
  const RouteDecision& decision = commit.decision;
  const ProfileSpec* profile =
    ProfileRegistry::find(decision.pipeline.profileId);
  if (!commit.routeFinalized || !decision.selected || profile == nullptr ||
      decision.pipeline.profileRevision != profile->revision ||
      !ProfileRegistry::allowsEvidence(*profile, decision.evidence) ||
      !ProfileRegistry::allowsVariant(profile->id,
                                      decision.pipeline.variantId) ||
      !ProfileRegistry::allowsExpert(profile->id, decision.expert) ||
      !isImplementedExpert(decision.expert) ||
      decision.statePolicy == StatePolicy::FULL_SHADOW ||
      decision.statePolicy == StatePolicy::GLOBAL_LIGHT ||
      ((profile->requiresExactSchema ||
        isDestructiveTransform(decision.transform)) &&
       !evidenceMayUseSchemaTransform(decision.evidence)))
    quit("Routed commit unit has an invalid final profile decision.");

  // Native CommitPlan never embeds a whole legacy archive or selects the old
  // profile-labelled PAQ pseudo experts. All PAQ blocks use the immutable
  // fragment contract; registered external experts are self-contained RESET
  // segments. This branch structure is also the stable admission point for
  // frozen OpenZL and a future container recipe executor.
  if (decision.expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1) {
    if (decision.transform != TransformId::NONE)
      quit("PAQ block fragments may not repeat an outer transform.");
    if (decision.statePolicy != StatePolicy::CONTINUE_GROUP ||
        commit.stateFamily != StateFamily::PAQ_MODEL_SESSION_V1)
      quit("PAQ block fragment lacks the archive-wide PAQ state contract.");
    if (!commit.reconstructionRecipe.empty())
      quit("Source-aligned PAQ block fragments may not carry a recipe.");
    return;
  }

  if (isSelfContainedExternalExpert(decision.expert)) {
    if (decision.statePolicy != StatePolicy::RESET ||
        commit.stateFamily != StateFamily::NONE ||
        !commit.reconstructionRecipe.empty())
      quit("Self-contained routed expert has a non-RESET state contract.");
    return;
  }

  if (decision.expert == ExpertId::CONTAINER_RECIPE_V1) {
    if (decision.statePolicy != StatePolicy::RESET ||
        commit.stateFamily != StateFamily::NONE ||
        commit.reconstructionRecipe.empty())
      quit("Container reconstruction commit has an invalid recipe contract.");
    return;
  }

  quit("Routed commit unit selects an unavailable native expert contract.");
}

inline void validateCommitUnit(const CommitUnit& commit) {
  if (commit.sourceOffset > std::numeric_limits<uint64_t>::max() -
        commit.sourceLength ||
      commit.firstBlockIndex > std::numeric_limits<size_t>::max() -
        commit.blockCount)
    quit("Routed commit unit has an overflowing source or block range.");

  const bool empty = commit.sourceLength == 0;
  if ((!empty && commit.blockCount == 0) ||
      (empty && (commit.sourceOffset != 0 || commit.firstBlockIndex != 0 ||
                 commit.blockCount != 0)))
    quit("Routed commit unit has an invalid source-to-block range.");

  validateCommittedDecision(commit);
  if (empty && commit.decision.expert != ExpertId::PAQ_BLOCK_FRAGMENT_V1)
    quit("An empty routed source requires the PAQ fragment contract.");

  // Routed wire v2 has no commitId/frameIndex fields.  Until a newer wire
  // contract publishes them, exactly one frame must equal the whole commit.
  if (commit.frames.size() != 1)
    quit("Routed wire v2 requires exactly one transport frame per commit.");
  const TransportFrame& frame = commit.frames.front();
  if (frame.commitId != commit.commitId || frame.frameIndex != 0 ||
      frame.sourceOffset != commit.sourceOffset ||
      frame.sourceLength != commit.sourceLength || !frame.finalFrame)
    quit("Routed transport frame does not exactly match its commit unit.");
}

inline std::vector<TransportFrame> makeTransportFrames(
    uint64_t commitId, uint64_t sourceOffset, uint64_t sourceLength,
    uint64_t maximumFrameBytes) {
  if (maximumFrameBytes == 0 ||
      sourceOffset > std::numeric_limits<uint64_t>::max() - sourceLength ||
      sourceLength > maximumFrameBytes)
    quit("Routed wire v2 cannot split this commit into multiple frames.");
  TransportFrame frame;
  frame.commitId = commitId;
  frame.frameIndex = 0;
  frame.sourceOffset = sourceOffset;
  frame.sourceLength = sourceLength;
  frame.finalFrame = true;
  return {frame};
}

class CommitPlan {
public:
  void append(CommitUnit commit) {
    if (sealed_)
      quit("Cannot append to a sealed routed commit plan.");
    units_.push_back(std::move(commit));
  }

  void seal(uint64_t totalSourceLength) {
    if (sealed_)
      quit("Routed commit plan was sealed more than once.");
    totalSourceLength_ = totalSourceLength;
    validateCoverageAndContracts();
    sealed_ = true;
  }

  bool sealed() const { return sealed_; }
  uint64_t totalSourceLength() const { return totalSourceLength_; }
  size_t size() const { return units_.size(); }
  bool empty() const { return units_.empty(); }

  const CommitUnit& at(size_t index) const {
    if (index >= units_.size())
      quit("Routed commit-plan index is outside its range.");
    return units_[index];
  }

  CommitUnit& mutableAt(size_t index) {
    if (sealed_)
      quit("Cannot mutate a sealed routed commit plan.");
    if (index >= units_.size())
      quit("Routed commit-plan index is outside its range.");
    return units_[index];
  }

  const std::vector<CommitUnit>& units() const { return units_; }

  void validateSealed() const {
    if (!sealed_)
      quit("Routed commit plan has not been sealed.");
    validateCoverageAndContracts();
  }

private:
  void validateCoverageAndContracts() const {
    if (units_.empty())
      quit("Routed commit plan contains no commit units.");

    if (totalSourceLength_ == 0) {
      if (units_.size() != 1 || units_.front().commitId != 0 ||
          units_.front().sourceOffset != 0 ||
          units_.front().sourceLength != 0)
        quit("Empty routed source must use one canonical empty commit.");
      validateCommitUnit(units_.front());
      return;
    }

    uint64_t expectedOffset = 0;
    size_t expectedBlockIndex = 0;
    for (size_t index = 0; index < units_.size(); ++index) {
      const CommitUnit& commit = units_[index];
      if (commit.commitId != index || commit.sourceOffset != expectedOffset ||
          commit.firstBlockIndex != expectedBlockIndex ||
          commit.sourceLength == 0)
        quit("Routed commit plan has a gap, overlap or noncanonical id.");
      validateCommitUnit(commit);
      expectedOffset += commit.sourceLength;
      expectedBlockIndex += commit.blockCount;
    }
    if (expectedOffset != totalSourceLength_)
      quit("Routed commit plan does not cover the source exactly.");
  }

  std::vector<CommitUnit> units_;
  uint64_t totalSourceLength_ = 0;
  bool sealed_ = false;
};

} // namespace routed
