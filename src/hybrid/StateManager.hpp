#pragma once

#include "../Utils.hpp"
#include "PaqConfig.hpp"
#include "ProfileTypes.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace routed {

constexpr uint32_t kMaximumLiveStates = 1024;

// New routed-native states are keyed by a state family and its complete
// canonical decoder configuration. Pipeline/profile and expert identifiers
// are intentionally excluded: PAQ default, text, image and machine-code
// fragments all advance the same predictor/model session.
inline uint64_t stateCompatibilityHash(
    StateFamily family, const std::vector<uint8_t>& canonicalConfig) {
  uint64_t hash = UINT64_C(1469598103934665603);
  const auto mixByte = [&hash](uint8_t byte) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  };
  const uint16_t familyValue = static_cast<uint16_t>(family);
  mixByte('S');
  mixByte('T');
  mixByte(static_cast<uint8_t>(familyValue));
  mixByte(static_cast<uint8_t>(familyValue >> 8));
  for (uint8_t byte : canonicalConfig)
    mixByte(byte);
  // The routed header reserves zero to mean "no continuable state".
  return hash == 0 ? UINT64_C(0xcbf29ce484222325) : hash;
}

// Decode-only compatibility helper for metadata produced before state-family
// contracts existed. New native routed writers must use the overload above.
inline uint64_t stateCompatibilityHash(
    const PipelineKey& pipeline, ExpertId expert,
    const std::vector<uint8_t>& criticalParameters) {
  uint64_t hash = UINT64_C(1469598103934665603);
  const auto mixByte = [&hash](uint8_t byte) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  };
  const uint16_t profile = static_cast<uint16_t>(pipeline.profileId);
  const uint16_t expertValue = static_cast<uint16_t>(expert);
  mixByte(static_cast<uint8_t>(profile));
  mixByte(static_cast<uint8_t>(profile >> 8));
  mixByte(pipeline.profileRevision);
  mixByte(pipeline.variantId);
  mixByte(static_cast<uint8_t>(expertValue));
  mixByte(static_cast<uint8_t>(expertValue >> 8));
  for (uint8_t byte : criticalParameters)
    mixByte(byte);
  return hash == 0 ? UINT64_C(0xcbf29ce484222325) : hash;
}

struct StateDescriptor {
  uint32_t stateId = 0;
  StateFamily family = StateFamily::NONE;
  uint64_t compatibilityHash = 0;
  StatePolicy policy = StatePolicy::RESET;

  PaqConfigId configId{};
  std::vector<uint8_t> canonicalConfig;

  // Diagnostic/legacy metadata. These fields are preserved so the existing
  // legacy-envelope adapter remains source-compatible, but they are not part
  // of native state continuation compatibility.
  PipelineKey pipeline;
  ExpertId expert = ExpertId::PAQ_LEGACY_ARCHIVE;
};

inline bool sameStateContract(const StateDescriptor& left,
                              const StateDescriptor& right) {
  return left.family == right.family && left.policy == right.policy &&
         left.compatibilityHash == right.compatibilityHash &&
         left.configId == right.configId &&
         canonicalPaqConfigEquals(left.canonicalConfig,
                                  right.canonicalConfig);
}

inline bool validStateContract(const StateDescriptor& descriptor) {
  if (descriptor.family != StateFamily::PAQ_MODEL_SESSION_V1 ||
      descriptor.policy != StatePolicy::CONTINUE_GROUP ||
      descriptor.expert != ExpertId::PAQ_BLOCK_FRAGMENT_V1 ||
      descriptor.stateId == 0 || descriptor.compatibilityHash == 0 ||
      descriptor.canonicalConfig.empty() ||
      isZeroPaqConfigId(descriptor.configId))
    return false;

  PaqConfigV1 parsed;
  if (!decodePaqConfigCanonical(descriptor.canonicalConfig, parsed) ||
      makePaqConfigId(descriptor.canonicalConfig) != descriptor.configId ||
      stateCompatibilityHash(descriptor.family,
                             descriptor.canonicalConfig) !=
        descriptor.compatibilityHash)
    return false;
  return true;
}

inline StateDescriptor makePaqStateDescriptor(
    uint32_t stateId, const PipelineKey& pipeline,
    const std::vector<uint8_t>& canonicalConfig) {
  StateDescriptor descriptor;
  descriptor.stateId = stateId;
  descriptor.family = StateFamily::PAQ_MODEL_SESSION_V1;
  descriptor.compatibilityHash =
    stateCompatibilityHash(descriptor.family, canonicalConfig);
  descriptor.policy = StatePolicy::CONTINUE_GROUP;
  descriptor.configId = makePaqConfigId(canonicalConfig);
  descriptor.canonicalConfig = canonicalConfig;
  descriptor.pipeline = pipeline;
  descriptor.expert = ExpertId::PAQ_BLOCK_FRAGMENT_V1;
  if (!validStateContract(descriptor))
    quit("Cannot construct an invalid PAQ routed-state descriptor.");
  return descriptor;
}

inline StateDescriptor makePaqStateDescriptor(
    uint32_t stateId, const PipelineKey& pipeline,
    const PaqConfigV1& config) {
  return makePaqStateDescriptor(stateId, pipeline,
                                encodePaqConfigCanonical(config));
}

class StateManager {
public:
  const StateDescriptor* find(uint32_t stateId) const {
    for (const StateDescriptor& state : states_) {
      if (state.stateId == stateId)
        return &state;
    }
    return nullptr;
  }

  const StateDescriptor* findFamily(StateFamily family) const {
    for (const StateDescriptor& state : states_) {
      if (state.family == family)
        return &state;
    }
    return nullptr;
  }

  size_t activeCount() const { return states_.size(); }

  void beginSegment(uint16_t flags, const StateDescriptor& requested) {
    constexpr uint16_t knownFlags =
      segmentFlag(SegmentFlag::START) |
      segmentFlag(SegmentFlag::CONTINUE) |
      segmentFlag(SegmentFlag::END) |
      segmentFlag(SegmentFlag::RAW_ESCAPE);
    if ((flags & ~knownFlags) != 0)
      quit("Routed segment contains unknown state flags.");

    const bool start = hasSegmentFlag(flags, SegmentFlag::START);
    const bool continuation = hasSegmentFlag(flags, SegmentFlag::CONTINUE);
    const bool end = hasSegmentFlag(flags, SegmentFlag::END);
    const bool rawEscape = hasSegmentFlag(flags, SegmentFlag::RAW_ESCAPE);
    if (start == continuation)
      quit("Routed segment must contain exactly one of START or CONTINUE.");
    if (rawEscape)
      quit("RAW_ESCAPE is not a supported routed-native state transition.");

    if (requested.policy == StatePolicy::RESET) {
      if (!start || !end || requested.stateId != 0 ||
          requested.family != StateFamily::NONE ||
          requested.compatibilityHash != 0 ||
          !isZeroPaqConfigId(requested.configId) ||
          !requested.canonicalConfig.empty())
        quit("RESET segments must be self-contained START/END states.");
      return;
    }
    if (requested.policy == StatePolicy::FULL_SHADOW ||
        requested.policy == StatePolicy::GLOBAL_LIGHT)
      quit("Requested routed state policy is not implemented.");
    if (!validStateContract(requested))
      quit("Continuable routed state has an invalid family/config contract.");

    const StateDescriptor* existing = find(requested.stateId);
    if (start) {
      if (existing != nullptr)
        quit("Routed state id is already active.");
      // Stage 4 uses one archive-wide PAQ model session. A second PAQ state
      // would also be unsafe while PAQ model accessors remain process statics.
      if (findFamily(requested.family) != nullptr)
        quit("Routed archive starts a second live state in one state family.");
      if (states_.size() >= kMaximumLiveStates)
        quit("Routed archive exceeds the live-state limit.");
      states_.push_back(requested);
      return;
    }

    if (existing == nullptr || !sameStateContract(*existing, requested))
      quit("Routed CONTINUE state is missing or has a different config.");
  }

  void finishSegment(uint16_t flags, const StateDescriptor& descriptor) {
    if (descriptor.policy == StatePolicy::RESET)
      return;
    if (!hasSegmentFlag(flags, SegmentFlag::END))
      return;
    for (size_t index = 0; index < states_.size(); ++index) {
      if (states_[index].stateId == descriptor.stateId) {
        if (!sameStateContract(states_[index], descriptor))
          quit("Routed END state has a different family/config contract.");
        states_.erase(states_.begin() + static_cast<std::ptrdiff_t>(index));
        return;
      }
    }
    quit("Routed END references an inactive state.");
  }

  void requireAllClosed() const {
    if (!states_.empty())
      quit("Routed archive ended with live continuation states.");
  }

private:
  std::vector<StateDescriptor> states_;
};

} // namespace routed
