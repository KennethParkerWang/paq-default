#pragma once

#include "../Utils.hpp"
#include "ProfileTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace routed {

constexpr uint8_t evidenceBit(EvidenceKind evidence) {
  return static_cast<uint8_t>(1u << static_cast<uint8_t>(evidence));
}

constexpr uint8_t kExactEvidence =
  evidenceBit(EvidenceKind::EXACT_FORMAT) |
  evidenceBit(EvidenceKind::TRUSTED_DESCRIPTOR);
constexpr uint8_t kStrictEvidence = kExactEvidence |
  evidenceBit(EvidenceKind::STRICT_CONTENT);
constexpr uint8_t kAllEvidence = kStrictEvidence |
  evidenceBit(EvidenceKind::STATISTICAL_INFERENCE);

struct ProfileSpec {
  ProfileId id;
  uint8_t revision;
  uint8_t priorityTier;
  uint8_t allowedEvidence;
  uint64_t minimumBytes;
  bool changesByteOrder;
  bool requiresExactSchema;
  bool automaticRoutingEnabled;
  StatePolicy defaultStatePolicy;
  ExpertId defaultExpert;
};

constexpr std::array<ProfileSpec, 18> kProfileSpecs = {{
  {ProfileId::PAQ_DEFAULT, 1, 9, kAllEvidence, 0, false, false, true,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_DEFAULT},
  // Published numeric ids remain reserved so they cannot be reassigned. The
  // current routed-native encoder and decoder both reject this RAW contract;
  // older hybrid-v1 compatibility is handled by its separate legacy reader.
  {ProfileId::OPAQUE_STORE, 1, 2, kExactEvidence, 0, false, false, false,
   StatePolicy::RESET, ExpertId::RAW_STORE},

  {ProfileId::TEXT_UTF8, 1, 7, kStrictEvidence, 16u * 1024u, false, false, true,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_TEXT},
  {ProfileId::TEXT_WIDE, 1, 6, kStrictEvidence, 16u * 1024u, false, false, true,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_WIDE_TEXT},
  {ProfileId::TEXT_ROWS, 1, 5, kStrictEvidence, 64u * 1024u, false, false, true,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_ROWS},

  {ProfileId::RECORD_STRIDE_CTX, 1, 8,
   evidenceBit(EvidenceKind::TRUSTED_DESCRIPTOR) |
     evidenceBit(EvidenceKind::STATISTICAL_INFERENCE),
   256u * 1024u, false, false, true,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_RECORD},
  {ProfileId::RECORD_SCHEMA_COLUMNAR, 1, 4, kExactEvidence,
   64u * 1024u, true, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_RECORD},
  {ProfileId::DB_PAGE_EXACT, 1, 3, kExactEvidence,
   512, false, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_RECORD},

  {ProfileId::INT_ARRAY, 1, 3, kExactEvidence, 16, false, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_NUMERIC},
  {ProfileId::INT_TIMESERIES, 1, 3, kExactEvidence, 16, false, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_NUMERIC},
  {ProfileId::FLOAT_ARRAY, 1, 3, kExactEvidence, 16, false, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_NUMERIC},
  {ProfileId::FLOAT_TIMESERIES, 1, 3, kExactEvidence, 16, false, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_NUMERIC},

  {ProfileId::RASTER_IMAGE, 1, 3, kExactEvidence, 64, false, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_IMAGE},
  {ProfileId::PCM_AUDIO, 1, 3, kExactEvidence, 64, false, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_AUDIO},
  {ProfileId::RAW_VIDEO, 1, 3, kExactEvidence, 64, false, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_IMAGE},

  {ProfileId::MACHINE_CODE, 1, 3, kExactEvidence, 64, false, true, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_MACHINE_CODE},
  {ProfileId::RUN_SPARSE, 1, 8, kAllEvidence, 64u * 1024u, true, false, false,
   StatePolicy::CONTINUE_LOCAL, ExpertId::PAQ_DEFAULT},

  // Parser-owned container classification. The generic statistical router
  // may not select it; SourcePlanner emits it directly only after the exact
  // ZIP/ZIP64 layout parser has produced complete source coverage.
  {ProfileId::ZIP_CONTAINER_STRUCTURE, 1, 1, kExactEvidence, 1, false, true,
   false, StatePolicy::CONTINUE_GROUP,
   ExpertId::PAQ_BLOCK_FRAGMENT_V1}
}};

class ProfileRegistry {
public:
  static const ProfileSpec* find(ProfileId id) {
    for (const ProfileSpec& spec : kProfileSpecs) {
      if (spec.id == id)
        return &spec;
    }
    return nullptr;
  }

  static const ProfileSpec& require(ProfileId id) {
    const ProfileSpec* spec = find(id);
    if (spec == nullptr)
      quit("Unknown routed compression profile.");
    return *spec;
  }

  static bool allowsEvidence(const ProfileSpec& spec, EvidenceKind evidence) {
    return (spec.allowedEvidence & evidenceBit(evidence)) != 0;
  }

  static bool allowsExpert(ProfileId profile, ExpertId expert) {
    // PAQ block fragments are the common native routed payload for every
    // decoded-data profile. OPAQUE_STORE is only a reserved published id in
    // routed-native v2 and may never silently become a raw escape.
    if (expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1)
      return profile != ProfileId::OPAQUE_STORE && find(profile) != nullptr;

    switch (profile) {
      case ProfileId::PAQ_DEFAULT:
        return expert == ExpertId::PAQ_DEFAULT ||
               expert == ExpertId::PAQ_LEGACY_ARCHIVE;
      case ProfileId::OPAQUE_STORE:
        return expert == ExpertId::RAW_STORE;
      case ProfileId::TEXT_UTF8:
        return expert == ExpertId::PAQ_TEXT;
      case ProfileId::TEXT_WIDE:
        return expert == ExpertId::PAQ_WIDE_TEXT;
      case ProfileId::TEXT_ROWS:
        return expert == ExpertId::PAQ_ROWS || expert == ExpertId::PAQ_RECORD;
      case ProfileId::RECORD_STRIDE_CTX:
      case ProfileId::DB_PAGE_EXACT:
        return expert == ExpertId::PAQ_RECORD;
      case ProfileId::RECORD_SCHEMA_COLUMNAR:
        return expert == ExpertId::PAQ_RECORD || expert == ExpertId::PCO_V4 ||
               expert == ExpertId::OPENZL_FROZEN_V1;
      case ProfileId::INT_ARRAY:
      case ProfileId::INT_TIMESERIES:
        return expert == ExpertId::PAQ_NUMERIC || expert == ExpertId::PCO_V4 ||
               expert == ExpertId::SPRINTZ_V1 ||
               expert == ExpertId::GORILLA_CANONICAL_V1;
      case ProfileId::FLOAT_ARRAY:
      case ProfileId::FLOAT_TIMESERIES:
        return expert == ExpertId::PAQ_NUMERIC || expert == ExpertId::ALP_V1 ||
               expert == ExpertId::GORILLA_CANONICAL_V1 ||
               expert == ExpertId::DEXOR_FROZEN_V1;
      case ProfileId::RASTER_IMAGE:
        return expert == ExpertId::PAQ_IMAGE ||
               expert == ExpertId::JPEG_XL_ISO_18181;
      case ProfileId::PCM_AUDIO:
        return expert == ExpertId::PAQ_AUDIO || expert == ExpertId::FLAC_RFC9639 ||
               expert == ExpertId::WAVPACK_FROZEN_V1;
      case ProfileId::RAW_VIDEO:
        return expert == ExpertId::PAQ_IMAGE || expert == ExpertId::FFV1_VERSION_3;
      case ProfileId::MACHINE_CODE:
        return expert == ExpertId::PAQ_MACHINE_CODE;
      case ProfileId::RUN_SPARSE:
        return expert == ExpertId::PAQ_DEFAULT;
      case ProfileId::ZIP_CONTAINER_STRUCTURE:
        return expert == ExpertId::PAQ_DEFAULT ||
               expert == ExpertId::CONTAINER_RECIPE_V1;
    }
    return false;
  }

  static bool allowsVariant(ProfileId profile, uint8_t variantId) {
    return find(profile) != nullptr && variantId == 0;
  }

  static bool validateAutomaticDecision(const RouteDecision& decision,
                                        uint64_t sourceLength) {
    const ProfileSpec* spec = find(decision.pipeline.profileId);
    if (spec == nullptr || decision.pipeline.profileRevision != spec->revision ||
        !decision.selected || !spec->automaticRoutingEnabled ||
        sourceLength < spec->minimumBytes ||
        !allowsEvidence(*spec, decision.evidence) ||
        !allowsVariant(spec->id, decision.pipeline.variantId) ||
        !allowsExpert(spec->id, decision.expert) ||
        !isImplementedExpert(decision.expert))
      return false;
    if ((spec->requiresExactSchema || isDestructiveTransform(decision.transform)) &&
        !evidenceMayUseSchemaTransform(decision.evidence))
      return false;
    if (decision.statePolicy == StatePolicy::FULL_SHADOW ||
        decision.statePolicy == StatePolicy::GLOBAL_LIGHT)
      return false; // Neither shadow-update policy reaches PAQ models yet.
    if (decision.expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1 &&
        decision.statePolicy != StatePolicy::CONTINUE_GROUP)
      return false;
    if ((isSelfContainedExternalExpert(decision.expert) ||
         decision.expert == ExpertId::CONTAINER_RECIPE_V1) &&
        decision.statePolicy != StatePolicy::RESET)
      return false;
    return true;
  }

  static RouteDecision fallback() {
    RouteDecision decision;
    decision.pipeline = {ProfileId::PAQ_DEFAULT, 1, 0};
    decision.evidence = EvidenceKind::STATISTICAL_INFERENCE;
    decision.statePolicy = StatePolicy::CONTINUE_LOCAL;
    decision.expert = ExpertId::PAQ_DEFAULT;
    decision.transform = TransformId::NONE;
    decision.reasonCode = 0;
    decision.selected = true;
    return decision;
  }

  // Native routed fallback is distinct from fallback(): the latter describes
  // the existing in-stream PAQ decision used by Stage 0-3/legacy block plans.
  // This one is the immutable outer Segment decoder contract.
  static RouteDecision nativePaqFallback() {
    RouteDecision decision;
    decision.pipeline = {ProfileId::PAQ_DEFAULT, 1, 0};
    decision.evidence = EvidenceKind::STATISTICAL_INFERENCE;
    decision.statePolicy = StatePolicy::CONTINUE_GROUP;
    decision.expert = ExpertId::PAQ_BLOCK_FRAGMENT_V1;
    decision.transform = TransformId::NONE;
    decision.reasonCode = 0;
    decision.selected = true;
    return decision;
  }

  // Preserve an already proven descriptive profile when its chosen external
  // expert fails before commit, while replacing only the payload contract.
  static RouteDecision paqFragmentFor(const RouteDecision& requested) {
    const ProfileSpec* spec = find(requested.pipeline.profileId);
    RouteDecision decision = spec == nullptr ? nativePaqFallback() : requested;
    if (spec != nullptr) {
      decision.pipeline.profileRevision = spec->revision;
      decision.pipeline.variantId = 0;
      decision.statePolicy = StatePolicy::CONTINUE_GROUP;
      decision.expert = ExpertId::PAQ_BLOCK_FRAGMENT_V1;
      decision.transform = TransformId::NONE;
      decision.parameters.clear();
      decision.predictedSavingBytes = 0;
      decision.selected = true;
    }
    return decision;
  }
};

static_assert(kProfileSpecs.front().id == ProfileId::PAQ_DEFAULT,
              "The registry fallback entry must remain first.");

} // namespace routed
