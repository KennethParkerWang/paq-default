#pragma once

#include "../file/File.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace routed {

constexpr uint32_t kSaoSilesiaHeaderBytes = 28;
constexpr uint32_t kSaoSilesiaRecordBytes = 28;
constexpr uint32_t kSaoSilesiaRecordCount = 258997;
constexpr uint64_t kSaoSilesiaObjectBytes =
  uint64_t{kSaoSilesiaHeaderBytes} +
  uint64_t{kSaoSilesiaRecordBytes} * kSaoSilesiaRecordCount;

enum class SaoExtentKind : uint8_t {
  FULL_OBJECT = 0,
  EXPLICIT_PREFIX_FROM_ZERO = 1
};

struct SaoSchemaMatch {
  uint32_t parserId = 0x53414f31u; // "SAO1"
  uint16_t parserRevision = 1;
  uint32_t schemaId = 0x00010001u;
  SaoExtentKind extent = SaoExtentKind::FULL_OBJECT;
  uint64_t sourceOffset = 0;
  uint64_t sourceLength = 0;
  uint32_t headerBytes = kSaoSilesiaHeaderBytes;
  uint32_t recordBytes = kSaoSilesiaRecordBytes;
  uint32_t recordCount = 0;
  uint32_t tailBytes = 0;
};

inline uint32_t saoLoad32le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline bool matchSaoSilesiaHeader(const uint8_t* header) {
  // STARN is the final zero-based star number (258996), while the payload
  // contains 258997 records.  These are deliberately different constants.
  constexpr std::array<uint32_t, 7> expected = {
    0u, 1u, 258996u, 0u, 1u, 1u, 28u
  };
  for (size_t i = 0; i < expected.size(); ++i) {
    if (saoLoad32le(header + i * 4) != expected[i])
      return false;
  }
  return true;
}

// Automatic routing deliberately accepts only the frozen complete Silesia
// object. Prefix support requires an explicit caller contract and is not
// inferred merely because an input happens to end on or near a record boundary.
inline bool detectSaoSilesiaFull(File* source, uint64_t sourceOffset,
                                 uint64_t sourceLength,
                                 SaoSchemaMatch& result) {
  if (source == nullptr || sourceLength != kSaoSilesiaObjectBytes ||
      sourceOffset > std::numeric_limits<uint64_t>::max() - sourceLength)
    return false;
  const uint64_t savedPosition = source->curPos();
  source->setpos(sourceOffset);
  std::array<uint8_t, kSaoSilesiaHeaderBytes> header{};
  const bool read = source->blockRead(header.data(), header.size()) == header.size();
  source->setpos(savedPosition);
  if (!read || !matchSaoSilesiaHeader(header.data()))
    return false;
  result = {};
  result.sourceOffset = sourceOffset;
  result.sourceLength = sourceLength;
  result.recordCount = kSaoSilesiaRecordCount;
  return true;
}

} // namespace routed
