#pragma once

#include "ProfileParameters.hpp"
#include "SaoSchemaParser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace routed {

constexpr uint16_t kOpenZlFrozenExpertRevision = 1;
constexpr uint16_t kOpenZlDecoderContractVersion = 1;
constexpr uint32_t kOpenZlSaoPlanId = 0x00010001u;
constexpr uint32_t kOpenZlSaoSchemaId = 0x00010001u;
constexpr uint16_t kOpenZlWireRevision = 24;

// SHA-256 below is over this exact UTF-8 descriptor.  Keeping the descriptor
// beside the digest makes the frozen graph auditable without executing a
// runtime graph builder or a codec race.
constexpr char kOpenZlSaoPlanDescriptor[] =
  "openzl-v0.2.0|wire24|sao-published-v1|level1|header28-store|"
  "sra0:le-delta-fieldlz|sdec0:transpose-zstd|"
  "is-mag:num-token-huffman|xrpm-xdpm:struct-token-fieldlz|"
  "strict|no-auto-store";

struct OpenZlFrozenParamsV1 {
  uint16_t expertRevision = kOpenZlFrozenExpertRevision;
  uint16_t wireRevision = kOpenZlWireRevision;
  uint32_t planId = kOpenZlSaoPlanId;
  std::array<uint8_t, 32> planFingerprint{};
  uint32_t parserId = 0;
  uint16_t parserRevision = 0;
  uint32_t schemaId = kOpenZlSaoSchemaId;
  uint32_t headerBytes = 0;
  uint32_t recordBytes = 0;
  uint32_t recordCount = 0;
  uint32_t tailBytes = 0;
};

inline std::array<uint8_t, 32> openZlSaoPlanFingerprint() {
  // SHA-256 of kOpenZlSaoPlanDescriptor. It is frozen contract data, not a
  // platform-dependent runtime hash.
  return {{
    0xa4, 0xb3, 0x2b, 0x04, 0x54, 0xee, 0xde, 0xd7,
    0xe2, 0xeb, 0x99, 0xc2, 0xae, 0x3c, 0x2e, 0x7f,
    0xbc, 0x96, 0x07, 0xac, 0xac, 0x3c, 0x8d, 0xcc,
    0xb6, 0xe4, 0x03, 0xa6, 0xf8, 0x6e, 0x8f, 0x23
  }};
}

inline OpenZlFrozenParamsV1 makeOpenZlSaoParams(
    const SaoSchemaMatch& match) {
  OpenZlFrozenParamsV1 params;
  params.planFingerprint = openZlSaoPlanFingerprint();
  params.parserId = match.parserId;
  params.parserRevision = match.parserRevision;
  params.schemaId = match.schemaId;
  params.headerBytes = match.headerBytes;
  params.recordBytes = match.recordBytes;
  params.recordCount = match.recordCount;
  params.tailBytes = match.tailBytes;
  return params;
}

inline bool validOpenZlSaoParams(const OpenZlFrozenParamsV1& params,
                                 uint64_t decodedLength) {
  const uint64_t recordsBytes =
    uint64_t{params.recordBytes} * params.recordCount;
  return params.expertRevision == kOpenZlFrozenExpertRevision &&
         params.wireRevision == kOpenZlWireRevision &&
         params.planId == kOpenZlSaoPlanId &&
         params.planFingerprint == openZlSaoPlanFingerprint() &&
         params.parserId == 0x53414f31u && params.parserRevision == 1 &&
         params.schemaId == kOpenZlSaoSchemaId &&
         params.headerBytes == kSaoSilesiaHeaderBytes &&
         params.recordBytes == kSaoSilesiaRecordBytes &&
         params.recordCount == kSaoSilesiaRecordCount &&
         params.tailBytes == 0 &&
         uint64_t{params.headerBytes} + recordsBytes == decodedLength;
}

inline std::vector<uint8_t> encodeOpenZlFrozenParams(
    const OpenZlFrozenParamsV1& params) {
  if (!validOpenZlSaoParams(params, kSaoSilesiaObjectBytes))
    quit("Invalid frozen OpenZL SAO parameters.");
  CanonicalWriter writer;
  writer.putUleb128(params.expertRevision);
  writer.putUleb128(params.wireRevision);
  writer.putUleb128(params.planId);
  for (uint8_t byte : params.planFingerprint)
    writer.putByte(byte);
  writer.putUleb128(params.parserId);
  writer.putUleb128(params.parserRevision);
  writer.putUleb128(params.schemaId);
  writer.putUleb128(params.headerBytes);
  writer.putUleb128(params.recordBytes);
  writer.putUleb128(params.recordCount);
  writer.putUleb128(params.tailBytes);
  return writer.take();
}

inline bool decodeOpenZlFrozenParams(const std::vector<uint8_t>& bytes,
                                     uint64_t decodedLength,
                                     OpenZlFrozenParamsV1& params) {
  CanonicalReader reader(bytes);
  uint64_t expertRevision = 0, wireRevision = 0, planId = 0;
  uint64_t parserId = 0, parserRevision = 0, schemaId = 0;
  uint64_t headerBytes = 0, recordBytes = 0, recordCount = 0, tailBytes = 0;
  if (!reader.readUleb128(expertRevision) || expertRevision > UINT16_MAX ||
      !reader.readUleb128(wireRevision) || wireRevision > UINT16_MAX ||
      !reader.readUleb128(planId) || planId > UINT32_MAX)
    return false;
  for (uint8_t& byte : params.planFingerprint) {
    if (!reader.readByte(byte))
      return false;
  }
  if (!reader.readUleb128(parserId) || parserId > UINT32_MAX ||
      !reader.readUleb128(parserRevision) || parserRevision > UINT16_MAX ||
      !reader.readUleb128(schemaId) || schemaId > UINT32_MAX ||
      !reader.readUleb128(headerBytes) || headerBytes > UINT32_MAX ||
      !reader.readUleb128(recordBytes) || recordBytes > UINT32_MAX ||
      !reader.readUleb128(recordCount) || recordCount > UINT32_MAX ||
      !reader.readUleb128(tailBytes) || tailBytes > UINT32_MAX ||
      !reader.atEnd())
    return false;
  params.expertRevision = static_cast<uint16_t>(expertRevision);
  params.wireRevision = static_cast<uint16_t>(wireRevision);
  params.planId = static_cast<uint32_t>(planId);
  params.parserId = static_cast<uint32_t>(parserId);
  params.parserRevision = static_cast<uint16_t>(parserRevision);
  params.schemaId = static_cast<uint32_t>(schemaId);
  params.headerBytes = static_cast<uint32_t>(headerBytes);
  params.recordBytes = static_cast<uint32_t>(recordBytes);
  params.recordCount = static_cast<uint32_t>(recordCount);
  params.tailBytes = static_cast<uint32_t>(tailBytes);
  return validOpenZlSaoParams(params, decodedLength);
}

class OpenZlPlanRegistry {
public:
  static bool admitsSao(const SaoSchemaMatch& match) {
    return match.extent == SaoExtentKind::FULL_OBJECT &&
           match.sourceLength == kSaoSilesiaObjectBytes &&
           match.recordCount == kSaoSilesiaRecordCount &&
           match.tailBytes == 0;
  }

  static bool encoderAvailable() {
#if defined(PAQ_ENABLE_OPENZL)
    return true;
#else
    return false;
#endif
  }

  static bool decoderAvailable() {
#if defined(PAQ_ENABLE_OPENZL)
    return true;
#else
    return false;
#endif
  }
};

} // namespace routed
