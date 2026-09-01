#pragma once

#include "../Utils.hpp"
#include "../file/File.hpp"
#include "ProfileRegistry.hpp"
#include "StateManager.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace routed {

constexpr std::array<uint8_t, 8> kRoutedArchiveMagic = {
  'P', 'A', 'Q', 'X', 'R', 'P', '2', '\n'
};
constexpr std::array<uint8_t, 4> kRoutedSegmentMarker = {'R', 'S', 'G', '2'};
constexpr uint16_t kRoutedArchiveMajor = 2;
constexpr uint16_t kRoutedArchiveMinor = 0;
constexpr uint16_t kRoutedArchiveHeaderSize = 64;
constexpr uint16_t kRoutedSegmentHeaderVersion = 1;
constexpr uint16_t kRoutedSegmentHeaderSize = 128;
constexpr uint32_t kMinimumProfileRegistryVersion = 1;
constexpr uint32_t kProfileRegistryVersion = 2;
constexpr uint32_t kFeatureVersion = 1;
constexpr uint32_t kParserRegistryVersion = 1;
constexpr uint32_t kUnicodeTableVersion = 1;
constexpr uint32_t kMaximumRoutedSegments = 2u * 1000u * 1000u;
constexpr uint64_t kMaximumRoutedFieldLength = UINT64_C(1) << 40; // 1 TiB
constexpr uint64_t kMaximumParameterLength = UINT64_C(1) << 20;  // 1 MiB
constexpr uint64_t kMaximumRecipeLength = UINT64_C(8) << 20;     // 8 MiB

struct RoutedArchiveHeader {
  uint16_t flags = 0;
  uint32_t segmentCount = 0;
  uint32_t profileRegistryVersion = kProfileRegistryVersion;
  uint32_t featureVersion = kFeatureVersion;
  uint32_t encoderRuleSetId = 0;
  uint32_t parserRegistryVersion = kParserRegistryVersion;
  uint32_t unicodeTableVersion = kUnicodeTableVersion;
  uint64_t totalDecodedLength = 0;
};

struct RoutedSegmentHeader {
  SegmentKind kind = SegmentKind::ROUTED_PROFILE;
  EvidenceKind evidence = EvidenceKind::EXACT_FORMAT;
  StatePolicy statePolicy = StatePolicy::RESET;
  PipelineKey pipeline;
  ExpertId expert = ExpertId::PAQ_LEGACY_ARCHIVE;
  TransformId transform = TransformId::NONE;
  uint16_t flags = segmentFlag(SegmentFlag::START) |
                   segmentFlag(SegmentFlag::END);
  uint16_t decoderContractVersion = 1;
  uint32_t stateId = 0;
  uint32_t reasonCode = 0;
  int32_t scoreQ8 = 0;
  uint32_t predictedSavingBytes = 0;
  uint64_t decodedLength = 0;
  uint64_t payloadLength = 0;
  uint64_t parameterLength = 0;
  uint64_t recipeLength = 0;
  uint64_t stateCompatibility = 0;
  uint32_t decodedCrc32c = 0;
  uint32_t payloadCrc32c = 0;
  uint32_t parameterCrc32c = 0;
  uint32_t recipeCrc32c = 0;
  uint64_t segmentId = 0;
  uint64_t sourceOffset = 0;
};

class Crc32c {
public:
  Crc32c() : state_(0xffffffffu) {}

  void update(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      state_ ^= data[i];
      for (unsigned bit = 0; bit < 8; ++bit)
        state_ = (state_ >> 1) ^ (0x82f63b78u & (0u - (state_ & 1u)));
    }
  }

  uint32_t value() const { return state_ ^ 0xffffffffu; }

private:
  uint32_t state_;
};

inline uint32_t crc32c(const uint8_t* data, size_t size) {
  Crc32c crc;
  crc.update(data, size);
  return crc.value();
}

inline void routedStore16(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value);
  destination[1] = static_cast<uint8_t>(value >> 8);
}

inline void routedStore32(uint8_t* destination, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    destination[i] = static_cast<uint8_t>(value >> (8 * i));
}

inline void routedStore64(uint8_t* destination, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    destination[i] = static_cast<uint8_t>(value >> (8 * i));
}

inline uint16_t routedLoad16(const uint8_t* source) {
  return static_cast<uint16_t>(source[0]) |
         static_cast<uint16_t>(source[1]) << 8;
}

inline uint32_t routedLoad32(const uint8_t* source) {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i)
    value |= static_cast<uint32_t>(source[i]) << (8 * i);
  return value;
}

inline uint64_t routedLoad64(const uint8_t* source) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value |= static_cast<uint64_t>(source[i]) << (8 * i);
  return value;
}

inline void routedReadExact(File* input, uint8_t* destination, uint64_t size,
                            const char* errorMessage) {
  if (input == nullptr || (size != 0 && input->blockRead(destination, size) != size))
    quit(errorMessage);
}

inline void routedWriteExact(File* output, uint8_t* source, uint64_t size) {
  if (output == nullptr)
    quit("Routed archive output is unavailable.");
  if (size != 0)
    output->blockWrite(source, size);
}

inline bool hasRoutedArchiveMagic(File* input) {
  if (input == nullptr)
    return false;
  const uint64_t savedPosition = input->curPos();
  input->setpos(0);
  std::array<uint8_t, kRoutedArchiveMagic.size()> probe{};
  const bool matched = input->blockRead(probe.data(), probe.size()) == probe.size() &&
                       probe == kRoutedArchiveMagic;
  input->setpos(savedPosition);
  return matched;
}

inline void validateArchiveHeader(const RoutedArchiveHeader& header) {
  if (header.flags != 0)
    quit("Unsupported routed archive flags.");
  if (header.segmentCount == 0 || header.segmentCount > kMaximumRoutedSegments)
    quit("Routed archive segment count is outside its resource limit.");
  if (header.profileRegistryVersion < kMinimumProfileRegistryVersion ||
      header.profileRegistryVersion > kProfileRegistryVersion ||
      header.featureVersion > kFeatureVersion ||
      header.parserRegistryVersion > kParserRegistryVersion ||
      header.unicodeTableVersion > kUnicodeTableVersion)
    quit("Routed archive requires unsupported registry data.");
  if (header.totalDecodedLength > kMaximumRoutedFieldLength)
    quit("Routed archive decoded length is outside its resource limit.");
}

inline void writeRoutedArchiveHeader(File* output,
                                     const RoutedArchiveHeader& header) {
  validateArchiveHeader(header);
  std::array<uint8_t, kRoutedArchiveHeaderSize> encoded{};
  for (size_t i = 0; i < kRoutedArchiveMagic.size(); ++i)
    encoded[i] = kRoutedArchiveMagic[i];
  routedStore16(encoded.data() + 8, kRoutedArchiveMajor);
  routedStore16(encoded.data() + 10, kRoutedArchiveMinor);
  routedStore16(encoded.data() + 12, kRoutedArchiveHeaderSize);
  routedStore16(encoded.data() + 14, header.flags);
  routedStore32(encoded.data() + 16, header.segmentCount);
  routedStore32(encoded.data() + 20, header.profileRegistryVersion);
  routedStore32(encoded.data() + 24, header.featureVersion);
  routedStore32(encoded.data() + 28, header.encoderRuleSetId);
  routedStore32(encoded.data() + 32, header.parserRegistryVersion);
  routedStore32(encoded.data() + 36, header.unicodeTableVersion);
  routedStore64(encoded.data() + 40, header.totalDecodedLength);
  routedStore32(encoded.data() + 48, 0);
  routedStore32(encoded.data() + 48, crc32c(encoded.data(), encoded.size()));
  routedWriteExact(output, encoded.data(), encoded.size());
}

inline RoutedArchiveHeader readRoutedArchiveHeader(File* input) {
  std::array<uint8_t, kRoutedArchiveHeaderSize> encoded{};
  routedReadExact(input, encoded.data(), encoded.size(),
                  "Truncated routed archive header.");
  for (size_t i = 0; i < kRoutedArchiveMagic.size(); ++i) {
    if (encoded[i] != kRoutedArchiveMagic[i])
      quit("Invalid routed archive magic.");
  }
  const uint32_t storedCrc = routedLoad32(encoded.data() + 48);
  routedStore32(encoded.data() + 48, 0);
  if (crc32c(encoded.data(), encoded.size()) != storedCrc)
    quit("Routed archive header checksum mismatch.");
  if (routedLoad16(encoded.data() + 8) != kRoutedArchiveMajor ||
      routedLoad16(encoded.data() + 10) > kRoutedArchiveMinor ||
      routedLoad16(encoded.data() + 12) != kRoutedArchiveHeaderSize ||
      routedLoad32(encoded.data() + 52) != 0 ||
      routedLoad64(encoded.data() + 56) != 0)
    quit("Unsupported routed archive header version or reserved field.");

  RoutedArchiveHeader header;
  header.flags = routedLoad16(encoded.data() + 14);
  header.segmentCount = routedLoad32(encoded.data() + 16);
  header.profileRegistryVersion = routedLoad32(encoded.data() + 20);
  header.featureVersion = routedLoad32(encoded.data() + 24);
  header.encoderRuleSetId = routedLoad32(encoded.data() + 28);
  header.parserRegistryVersion = routedLoad32(encoded.data() + 32);
  header.unicodeTableVersion = routedLoad32(encoded.data() + 36);
  header.totalDecodedLength = routedLoad64(encoded.data() + 40);
  validateArchiveHeader(header);
  return header;
}

inline bool knownSegmentKind(uint8_t value) {
  return value <= static_cast<uint8_t>(SegmentKind::CONTAINER_MAP);
}

inline void validateSegmentHeader(const RoutedSegmentHeader& header) {
  const ProfileSpec* profile = ProfileRegistry::find(header.pipeline.profileId);
  if (profile == nullptr || header.pipeline.profileRevision != profile->revision)
    quit("Routed segment uses an unknown profile revision.");
  if (static_cast<uint8_t>(header.kind) >
        static_cast<uint8_t>(SegmentKind::CONTAINER_MAP) ||
      static_cast<uint8_t>(header.evidence) >
        static_cast<uint8_t>(EvidenceKind::STATISTICAL_INFERENCE) ||
      static_cast<uint8_t>(header.statePolicy) >
        static_cast<uint8_t>(StatePolicy::FULL_SHADOW) ||
      static_cast<uint16_t>(header.transform) >
        static_cast<uint16_t>(TransformId::RUN_GAP))
    quit("Routed segment contains an invalid enum value.");
  if (header.decoderContractVersion == 0)
    quit("Routed segment decoder contract version is zero.");
  if (!ProfileRegistry::allowsEvidence(*profile, header.evidence) ||
      !ProfileRegistry::allowsVariant(profile->id, header.pipeline.variantId) ||
      !ProfileRegistry::allowsExpert(profile->id, header.expert))
    quit("Routed segment evidence or expert is incompatible with its profile.");
  if ((profile->requiresExactSchema || isDestructiveTransform(header.transform)) &&
      !evidenceMayUseSchemaTransform(header.evidence))
    quit("Routed segment lacks exact evidence for its schema transform.");
  if (header.statePolicy == StatePolicy::FULL_SHADOW)
    quit("Routed segment requests unsupported FULL_SHADOW state.");
  constexpr uint16_t knownFlags =
    segmentFlag(SegmentFlag::START) | segmentFlag(SegmentFlag::CONTINUE) |
    segmentFlag(SegmentFlag::END) | segmentFlag(SegmentFlag::RAW_ESCAPE);
  const bool starts = hasSegmentFlag(header.flags, SegmentFlag::START);
  const bool continues = hasSegmentFlag(header.flags, SegmentFlag::CONTINUE);
  if ((header.flags & ~knownFlags) != 0 || starts == continues ||
      (hasSegmentFlag(header.flags, SegmentFlag::RAW_ESCAPE) &&
       !hasSegmentFlag(header.flags, SegmentFlag::END)))
    quit("Routed segment contains invalid state flags.");
  if ((header.statePolicy == StatePolicy::RESET &&
      (!starts || header.stateId != 0 || header.stateCompatibility != 0)) ||
      (header.statePolicy != StatePolicy::RESET &&
       (header.stateId == 0 || header.stateCompatibility == 0)))
    quit("Routed segment state identity is incompatible with its policy.");
  if (header.decodedLength > kMaximumRoutedFieldLength ||
      header.payloadLength > kMaximumRoutedFieldLength ||
      header.parameterLength > kMaximumParameterLength ||
      header.recipeLength > kMaximumRecipeLength)
    quit("Routed segment length is outside its resource limit.");
  if (header.payloadLength > std::numeric_limits<uint64_t>::max() -
        header.parameterLength ||
      header.payloadLength + header.parameterLength >
        std::numeric_limits<uint64_t>::max() - header.recipeLength)
    quit("Routed segment aggregate length overflows.");
  if (header.kind == SegmentKind::CONTAINER_MAP &&
      header.recipeLength == 0)
    quit("Container-map segment is missing its reconstruction recipe.");
}

inline void writeRoutedSegmentHeader(File* output,
                                     const RoutedSegmentHeader& header) {
  validateSegmentHeader(header);
  std::array<uint8_t, kRoutedSegmentHeaderSize> encoded{};
  for (size_t i = 0; i < kRoutedSegmentMarker.size(); ++i)
    encoded[i] = kRoutedSegmentMarker[i];
  routedStore16(encoded.data() + 4, kRoutedSegmentHeaderVersion);
  routedStore16(encoded.data() + 6, kRoutedSegmentHeaderSize);
  encoded[8] = static_cast<uint8_t>(header.kind);
  encoded[9] = static_cast<uint8_t>(header.evidence);
  encoded[10] = static_cast<uint8_t>(header.statePolicy);
  encoded[11] = header.pipeline.profileRevision;
  routedStore16(encoded.data() + 12,
                static_cast<uint16_t>(header.pipeline.profileId));
  routedStore16(encoded.data() + 14, static_cast<uint16_t>(header.expert));
  routedStore16(encoded.data() + 16, static_cast<uint16_t>(header.transform));
  routedStore16(encoded.data() + 18, header.flags);
  encoded[20] = header.pipeline.variantId;
  routedStore16(encoded.data() + 22, header.decoderContractVersion);
  routedStore32(encoded.data() + 24, header.stateId);
  routedStore32(encoded.data() + 28, header.reasonCode);
  routedStore32(encoded.data() + 32, static_cast<uint32_t>(header.scoreQ8));
  routedStore32(encoded.data() + 36, header.predictedSavingBytes);
  routedStore64(encoded.data() + 40, header.decodedLength);
  routedStore64(encoded.data() + 48, header.payloadLength);
  routedStore64(encoded.data() + 56, header.parameterLength);
  routedStore64(encoded.data() + 64, header.recipeLength);
  routedStore64(encoded.data() + 72, header.stateCompatibility);
  routedStore32(encoded.data() + 80, header.decodedCrc32c);
  routedStore32(encoded.data() + 84, header.payloadCrc32c);
  routedStore32(encoded.data() + 88, header.parameterCrc32c);
  routedStore32(encoded.data() + 92, header.recipeCrc32c);
  routedStore32(encoded.data() + 96, 0);
  routedStore64(encoded.data() + 104, header.segmentId);
  routedStore64(encoded.data() + 112, header.sourceOffset);
  routedStore32(encoded.data() + 96, crc32c(encoded.data(), encoded.size()));
  routedWriteExact(output, encoded.data(), encoded.size());
}

inline RoutedSegmentHeader readRoutedSegmentHeader(File* input) {
  std::array<uint8_t, kRoutedSegmentHeaderSize> encoded{};
  routedReadExact(input, encoded.data(), encoded.size(),
                  "Truncated routed segment header.");
  for (size_t i = 0; i < kRoutedSegmentMarker.size(); ++i) {
    if (encoded[i] != kRoutedSegmentMarker[i])
      quit("Invalid routed segment marker.");
  }
  const uint32_t storedCrc = routedLoad32(encoded.data() + 96);
  routedStore32(encoded.data() + 96, 0);
  if (crc32c(encoded.data(), encoded.size()) != storedCrc)
    quit("Routed segment header checksum mismatch.");
  if (routedLoad16(encoded.data() + 4) != kRoutedSegmentHeaderVersion ||
      routedLoad16(encoded.data() + 6) != kRoutedSegmentHeaderSize ||
      encoded[21] != 0 || routedLoad32(encoded.data() + 100) != 0 ||
      routedLoad64(encoded.data() + 120) != 0)
    quit("Unsupported routed segment version or reserved field.");

  RoutedSegmentHeader header;
  if (!knownSegmentKind(encoded[8]))
    quit("Unknown routed segment kind.");
  header.kind = static_cast<SegmentKind>(encoded[8]);
  header.evidence = static_cast<EvidenceKind>(encoded[9]);
  header.statePolicy = static_cast<StatePolicy>(encoded[10]);
  header.pipeline.profileRevision = encoded[11];
  header.pipeline.profileId = static_cast<ProfileId>(routedLoad16(encoded.data() + 12));
  header.expert = static_cast<ExpertId>(routedLoad16(encoded.data() + 14));
  header.transform = static_cast<TransformId>(routedLoad16(encoded.data() + 16));
  header.flags = routedLoad16(encoded.data() + 18);
  header.pipeline.variantId = encoded[20];
  header.decoderContractVersion = routedLoad16(encoded.data() + 22);
  header.stateId = routedLoad32(encoded.data() + 24);
  header.reasonCode = routedLoad32(encoded.data() + 28);
  header.scoreQ8 = static_cast<int32_t>(routedLoad32(encoded.data() + 32));
  header.predictedSavingBytes = routedLoad32(encoded.data() + 36);
  header.decodedLength = routedLoad64(encoded.data() + 40);
  header.payloadLength = routedLoad64(encoded.data() + 48);
  header.parameterLength = routedLoad64(encoded.data() + 56);
  header.recipeLength = routedLoad64(encoded.data() + 64);
  header.stateCompatibility = routedLoad64(encoded.data() + 72);
  header.decodedCrc32c = routedLoad32(encoded.data() + 80);
  header.payloadCrc32c = routedLoad32(encoded.data() + 84);
  header.parameterCrc32c = routedLoad32(encoded.data() + 88);
  header.recipeCrc32c = routedLoad32(encoded.data() + 92);
  header.segmentId = routedLoad64(encoded.data() + 104);
  header.sourceOffset = routedLoad64(encoded.data() + 112);
  validateSegmentHeader(header);
  return header;
}

} // namespace routed
