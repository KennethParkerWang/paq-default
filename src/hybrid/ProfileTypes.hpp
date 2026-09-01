#pragma once

#include <cstdint>
#include <vector>

namespace routed {

// ProfileId describes decoded data structure. It does not identify a file
// format and it does not by itself select a transform or codec.
enum class ProfileId : uint16_t {
  PAQ_DEFAULT = 0x0000,
  OPAQUE_STORE = 0x0001,

  TEXT_UTF8 = 0x0100,
  TEXT_WIDE = 0x0101,
  TEXT_ROWS = 0x0110,

  RECORD_STRIDE_CTX = 0x0200,
  RECORD_SCHEMA_COLUMNAR = 0x0201,
  DB_PAGE_EXACT = 0x0210,

  INT_ARRAY = 0x0300,
  INT_TIMESERIES = 0x0301,
  FLOAT_ARRAY = 0x0310,
  FLOAT_TIMESERIES = 0x0311,

  RASTER_IMAGE = 0x0400,
  PCM_AUDIO = 0x0410,
  RAW_VIDEO = 0x0420,

  MACHINE_CODE = 0x0500,
  RUN_SPARSE = 0x0600,

  // Exact container structure bytes (headers, descriptors, directories and
  // trailers), not the recursively planned member payloads.  The first ZIP
  // implementation keeps these bytes source-aligned and PAQ coded.
  ZIP_CONTAINER_STRUCTURE = 0x0700
};

enum class EvidenceKind : uint8_t {
  EXACT_FORMAT = 0,
  TRUSTED_DESCRIPTOR = 1,
  STRICT_CONTENT = 2,
  STATISTICAL_INFERENCE = 3
};

enum class SegmentKind : uint8_t {
  LEGACY_PAQ_BLOCK = 0,
  ROUTED_PROFILE = 1,
  CONTAINER_MAP = 2
};

enum class StatePolicy : uint8_t {
  RESET = 0,
  CONTINUE_LOCAL = 1,
  CONTINUE_GROUP = 2,
  GLOBAL_LIGHT = 3,
  FULL_SHADOW = 4
};

// A state family names the mutable decoder state shared by otherwise
// different routed profiles.  Compatibility is deliberately not tied to a
// PipelineKey or ExpertId: PAQ text/image/default fragments all advance the
// same archive-wide predictor session.
enum class StateFamily : uint16_t {
  NONE = 0x0000,
  PAQ_MODEL_SESSION_V1 = 0x0001
};

enum class TransformId : uint16_t {
  NONE = 0x0000,
  BYTE_PLANE = 0x0001,
  BIT_PLANE = 0x0002,
  DELTA = 0x0003,
  DELTA2 = 0x0004,
  RECORD_COLUMNAR = 0x0005,
  SPATIAL_RESIDUAL = 0x0006,
  CHANNEL_DECORRELATION = 0x0007,
  BRANCH_NORMALIZE = 0x0008,
  RUN_GAP = 0x0009
};

// Expert values are immutable decoder contracts. Reserved experts are named
// here so future archives need not renumber them, but registration does not
// imply that an encoder or decoder implementation is available.
enum class ExpertId : uint16_t {
  PAQ_LEGACY_ARCHIVE = 0x0000,
  PAQ_DEFAULT = 0x0001,
  PAQ_TEXT = 0x0002,
  PAQ_WIDE_TEXT = 0x0003,
  PAQ_ROWS = 0x0004,
  PAQ_RECORD = 0x0005,
  PAQ_NUMERIC = 0x0006,
  PAQ_IMAGE = 0x0007,
  PAQ_AUDIO = 0x0008,
  PAQ_MACHINE_CODE = 0x0009,
  // A fresh arithmetic payload containing one or more existing PAQ block
  // headers/payloads while predictor/model state lives in StateFamily::
  // PAQ_MODEL_SESSION_V1.  This is not the legacy inner archive contract.
  PAQ_BLOCK_FRAGMENT_V1 = 0x000a,

  PCO_V4 = 0x0100,
  SPRINTZ_V1 = 0x0101,
  ALP_V1 = 0x0102,
  GORILLA_CANONICAL_V1 = 0x0103,
  DEXOR_FROZEN_V1 = 0x0104,

  FLAC_RFC9639 = 0x0200,
  WAVPACK_FROZEN_V1 = 0x0201,
  JPEG_XL_ISO_18181 = 0x0202,
  FFV1_VERSION_3 = 0x0203,

  // Frozen OpenZL v0.2.0 frame contract. It is implemented only in builds that
  // link the pinned dependency and define PAQ_ENABLE_OPENZL; other builds
  // reject such archives and keep SAO on the PAQ fallback.
  OPENZL_FROZEN_V1 = 0x0300,

  // Reserved for a future non-source-aligned container reconstruction map.
  // The first ZIP implementation must use source-aligned leaf segments and
  // must not emit this contract.
  CONTAINER_RECIPE_V1 = 0x0400,

  RAW_STORE = 0x7fff
};

enum class SegmentFlag : uint16_t {
  START = 1u << 0,
  CONTINUE = 1u << 1,
  END = 1u << 2,
  RAW_ESCAPE = 1u << 3
};

constexpr uint16_t segmentFlag(SegmentFlag flag) {
  return static_cast<uint16_t>(flag);
}

constexpr bool hasSegmentFlag(uint16_t flags, SegmentFlag flag) {
  return (flags & segmentFlag(flag)) != 0;
}

struct PipelineKey {
  ProfileId profileId = ProfileId::PAQ_DEFAULT;
  uint8_t profileRevision = 1;
  uint8_t variantId = 0;

  bool operator==(const PipelineKey& other) const {
    return profileId == other.profileId &&
           profileRevision == other.profileRevision &&
           variantId == other.variantId;
  }

  bool operator!=(const PipelineKey& other) const { return !(*this == other); }
};

struct RouteDecision {
  PipelineKey pipeline;
  EvidenceKind evidence = EvidenceKind::STATISTICAL_INFERENCE;
  StatePolicy statePolicy = StatePolicy::RESET;
  ExpertId expert = ExpertId::PAQ_LEGACY_ARCHIVE;
  TransformId transform = TransformId::NONE;
  std::vector<uint8_t> parameters;
  uint32_t reasonCode = 0;
  int32_t scoreQ8 = 0;
  uint32_t predictedSavingBytes = 0;
  bool selected = false;
};

constexpr bool isDestructiveTransform(TransformId transform) {
  return transform != TransformId::NONE;
}

constexpr bool evidenceMayUseSchemaTransform(EvidenceKind evidence) {
  return evidence == EvidenceKind::EXACT_FORMAT ||
         evidence == EvidenceKind::TRUSTED_DESCRIPTOR;
}

constexpr bool isImplementedExpert(ExpertId expert) {
  const bool builtIn = expert == ExpertId::PAQ_LEGACY_ARCHIVE ||
         expert == ExpertId::PAQ_DEFAULT ||
         expert == ExpertId::PAQ_TEXT ||
         expert == ExpertId::PAQ_WIDE_TEXT ||
         expert == ExpertId::PAQ_ROWS ||
         expert == ExpertId::PAQ_RECORD ||
         expert == ExpertId::PAQ_NUMERIC ||
         expert == ExpertId::PAQ_IMAGE ||
         expert == ExpertId::PAQ_AUDIO ||
         expert == ExpertId::PAQ_MACHINE_CODE ||
         expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1;
#if defined(PAQ_ENABLE_OPENZL)
  return builtIn || expert == ExpertId::OPENZL_FROZEN_V1;
#else
  return builtIn;
#endif
}

constexpr bool isPaqModelExpert(ExpertId expert) {
  return expert == ExpertId::PAQ_DEFAULT ||
         expert == ExpertId::PAQ_TEXT ||
         expert == ExpertId::PAQ_WIDE_TEXT ||
         expert == ExpertId::PAQ_ROWS ||
         expert == ExpertId::PAQ_RECORD ||
         expert == ExpertId::PAQ_NUMERIC ||
         expert == ExpertId::PAQ_IMAGE ||
         expert == ExpertId::PAQ_AUDIO ||
         expert == ExpertId::PAQ_MACHINE_CODE ||
         expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1;
}

constexpr bool isSelfContainedExternalExpert(ExpertId expert) {
  return expert == ExpertId::OPENZL_FROZEN_V1 ||
         expert == ExpertId::PCO_V4 ||
         expert == ExpertId::SPRINTZ_V1 ||
         expert == ExpertId::ALP_V1 ||
         expert == ExpertId::GORILLA_CANONICAL_V1 ||
         expert == ExpertId::DEXOR_FROZEN_V1 ||
         expert == ExpertId::FLAC_RFC9639 ||
         expert == ExpertId::WAVPACK_FROZEN_V1 ||
         expert == ExpertId::JPEG_XL_ISO_18181 ||
         expert == ExpertId::FFV1_VERSION_3;
}

constexpr bool isRawStorageRoute(ProfileId profile, ExpertId expert) {
  return profile == ProfileId::OPAQUE_STORE || expert == ExpertId::RAW_STORE;
}

static_assert(static_cast<uint16_t>(ProfileId::PAQ_DEFAULT) == 0x0000,
              "Published profile identifiers must not change.");
static_assert(static_cast<uint16_t>(ProfileId::RECORD_STRIDE_CTX) == 0x0200,
              "Published profile identifiers must not change.");
static_assert(static_cast<uint16_t>(ProfileId::ZIP_CONTAINER_STRUCTURE) ==
                0x0700,
              "Published profile identifiers must not change.");
static_assert(static_cast<uint16_t>(ExpertId::PAQ_BLOCK_FRAGMENT_V1) == 0x000a,
              "Published expert identifiers must not change.");
static_assert(static_cast<uint16_t>(ExpertId::OPENZL_FROZEN_V1) == 0x0300,
              "Published expert identifiers must not change.");
static_assert(static_cast<uint16_t>(ExpertId::CONTAINER_RECIPE_V1) == 0x0400,
              "Published expert identifiers must not change.");
static_assert(static_cast<uint16_t>(ExpertId::RAW_STORE) == 0x7fff,
              "Published expert identifiers must not change.");

} // namespace routed
