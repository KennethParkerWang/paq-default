#pragma once

#include "ProfileRegistry.hpp"

#include <cstdint>

namespace routed {

constexpr uint32_t kProductionRuleSetId = 0x00010000u;
constexpr uint32_t kNativeRoutedRuleSetId = 0x00020001u;
constexpr uint64_t kStatisticalRoutingMinimumBytes = 256u * 1024u;
constexpr uint64_t kWideTextRoutingMinimumBytes = 16u * 1024u;
constexpr uint8_t kMaximumSamplingWindows = 4;

struct FrozenRoutingFeatures {
  uint32_t ruleSetId = 0;
  bool nativeSingleFile = false;
  bool zipStoredMembers = false;
  bool openZlSao = false;
  bool exactX86ExecutableSections = false;
};

inline const FrozenRoutingFeatures& nativeRoutedFeatures() {
  static const FrozenRoutingFeatures features = {
    kNativeRoutedRuleSetId, true, true, true, true
  };
  return features;
}

inline bool isSupportedEncoderRuleSet(uint32_t ruleSetId) {
  return ruleSetId == kProductionRuleSetId ||
         ruleSetId == kNativeRoutedRuleSetId;
}

inline uint8_t evidencePriority(EvidenceKind evidence) {
  return static_cast<uint8_t>(
    static_cast<uint8_t>(EvidenceKind::STATISTICAL_INFERENCE) -
    static_cast<uint8_t>(evidence));
}

// This comparator is used only after every profile's hard conditions pass.
// It never permits a statistical candidate to override exact evidence.
inline bool routePrecedes(const RouteDecision& candidate,
                          const RouteDecision& current) {
  const ProfileSpec& candidateSpec =
    ProfileRegistry::require(candidate.pipeline.profileId);
  const ProfileSpec& currentSpec =
    ProfileRegistry::require(current.pipeline.profileId);
  const uint8_t candidateEvidence = evidencePriority(candidate.evidence);
  const uint8_t currentEvidence = evidencePriority(current.evidence);
  if (candidateEvidence != currentEvidence)
    return candidateEvidence > currentEvidence;
  if (candidateSpec.priorityTier != currentSpec.priorityTier)
    return candidateSpec.priorityTier < currentSpec.priorityTier;
  if (candidate.predictedSavingBytes != current.predictedSavingBytes)
    return candidate.predictedSavingBytes > current.predictedSavingBytes;
  if (candidate.scoreQ8 != current.scoreQ8)
    return candidate.scoreQ8 > current.scoreQ8;
  if (candidate.pipeline.profileId != current.pipeline.profileId)
    return static_cast<uint16_t>(candidate.pipeline.profileId) <
           static_cast<uint16_t>(current.pipeline.profileId);
  return candidate.pipeline.variantId < current.pipeline.variantId;
}

} // namespace routed
