#pragma once

#include "../Utils.hpp"
#include "../file/File.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace hybrid {

// These identifiers are part of the on-disk decoder contract. Existing values
// must never be reassigned; new routes and contracts are appended instead.
enum class RouteId : uint16_t {
  RAW_STORED = 0x0001,
  PAQ_LEGACY_ARCHIVE = 0x0002,
  PAQ_V216_PAYLOAD = 0x0010,
  PAQ_TEXT_V1 = 0x0011,
  PAQ_RECORD_V1 = 0x0012,
  PAQ_NUMERIC_V1 = 0x0013,
  PAQ_RASTER_V1 = 0x0014,
  EMBEDDED_SPECIALIST = 0x0100
};

enum class DecoderContractId : uint32_t {
  RAW_STORED_V1 = 0x00010001u,
  PAQ8PX_LEGACY_ARCHIVE_V1 = 0x00020001u
};

static_assert(static_cast<uint16_t>(RouteId::RAW_STORED) == 0x0001,
              "Published route IDs must not change.");
static_assert(static_cast<uint16_t>(RouteId::PAQ_LEGACY_ARCHIVE) == 0x0002,
              "Published route IDs must not change.");
static_assert(static_cast<uint16_t>(RouteId::PAQ_V216_PAYLOAD) == 0x0010,
              "Reserved ExpertGraph backend IDs must not change.");
static_assert(static_cast<uint32_t>(DecoderContractId::RAW_STORED_V1) == 0x00010001u,
              "Published decoder contracts must not change.");
static_assert(static_cast<uint32_t>(DecoderContractId::PAQ8PX_LEGACY_ARCHIVE_V1) == 0x00020001u,
              "Published decoder contracts must not change.");

constexpr std::array<uint8_t, 8> kArchiveMagic = {
  'P', 'A', 'Q', 'X', 'E', 'G', '\r', '\n'
};
constexpr std::array<uint8_t, 4> kFrameMarker = {'F', 'R', 'M', '1'};
constexpr uint16_t kArchiveMajorVersion = 1;
constexpr uint16_t kArchiveMinorVersion = 0;
constexpr uint16_t kArchiveHeaderSize = 24;
constexpr uint16_t kFrameHeaderVersion = 1;
constexpr uint16_t kFrameHeaderSize = 64;
constexpr uint32_t kMaximumFrameCount = 1024u * 1024u;
constexpr uint64_t kMaximumFrameDataLength = UINT64_C(1) << 40; // 1 TiB per field in v1

// All integers are unsigned big-endian. Header CRC32 values cover the complete
// fixed-size header with that CRC field set to zero. A frame is header, payload,
// then recipe. payloadCrc32 covers exactly
// payloadLength bytes; decodedCrc32 covers exactly decodedLength backend output
// bytes. recipeCrc32 covers recipeLength bytes (zero for the Stage 0-3 routes).

struct ArchiveHeader {
  uint16_t majorVersion = kArchiveMajorVersion;
  uint16_t minorVersion = kArchiveMinorVersion;
  uint16_t flags = 0;
  uint32_t frameCount = 0;
};

struct FrameHeader {
  RouteId route = RouteId::RAW_STORED;
  uint16_t flags = 0;
  DecoderContractId decoderContract = DecoderContractId::RAW_STORED_V1;
  // Length produced by this backend. For PAQ_LEGACY_ARCHIVE this is the
  // complete inner archive length, not the final user-file length.
  uint64_t decodedLength = 0;
  uint64_t payloadLength = 0;
  uint64_t recipeLength = 0;
  uint32_t payloadCrc32 = 0;
  uint32_t decodedCrc32 = 0;
  uint32_t recipeCrc32 = 0;
};

class Crc32 {
public:
  Crc32() : state_(0xffffffffu) {}

  void update(const uint8_t* data, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      state_ ^= data[i];
      for (unsigned bit = 0; bit < 8; ++bit)
        state_ = (state_ >> 1) ^ (0xedb88320u & (0u - (state_ & 1u)));
    }
  }

  uint32_t value() const { return state_ ^ 0xffffffffu; }

private:
  uint32_t state_;
};

inline uint32_t crc32(const uint8_t* data, size_t size) {
  Crc32 crc;
  crc.update(data, size);
  return crc.value();
}

inline void store16(uint8_t* destination, uint16_t value) {
  destination[0] = static_cast<uint8_t>(value >> 8);
  destination[1] = static_cast<uint8_t>(value);
}

inline void store32(uint8_t* destination, uint32_t value) {
  destination[0] = static_cast<uint8_t>(value >> 24);
  destination[1] = static_cast<uint8_t>(value >> 16);
  destination[2] = static_cast<uint8_t>(value >> 8);
  destination[3] = static_cast<uint8_t>(value);
}

inline void store64(uint8_t* destination, uint64_t value) {
  for (unsigned i = 0; i < 8; ++i)
    destination[i] = static_cast<uint8_t>(value >> (56 - i * 8));
}

inline uint16_t load16(const uint8_t* source) {
  return static_cast<uint16_t>((static_cast<uint16_t>(source[0]) << 8) |
                               static_cast<uint16_t>(source[1]));
}

inline uint32_t load32(const uint8_t* source) {
  return (static_cast<uint32_t>(source[0]) << 24) |
         (static_cast<uint32_t>(source[1]) << 16) |
         (static_cast<uint32_t>(source[2]) << 8) |
         static_cast<uint32_t>(source[3]);
}

inline uint64_t load64(const uint8_t* source) {
  uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i)
    value = (value << 8) | source[i];
  return value;
}

inline void readExact(File* input, uint8_t* destination, uint64_t size,
                      const char* errorMessage) {
  if (input == nullptr || (size != 0 && input->blockRead(destination, size) != size))
    quit(errorMessage);
}

inline void writeExact(File* output, uint8_t* source, uint64_t size) {
  if (output == nullptr)
    quit("Hybrid archive output is not available.");
  if (size != 0)
    output->blockWrite(source, size);
}

inline bool hasArchiveMagic(File* input) {
  if (input == nullptr)
    return false;
  const uint64_t savedPosition = input->curPos();
  input->setpos(0);
  std::array<uint8_t, kArchiveMagic.size()> probe{};
  const bool matched = input->blockRead(probe.data(), probe.size()) == probe.size() &&
                       probe == kArchiveMagic;
  input->setpos(savedPosition);
  return matched;
}

inline void writeArchiveHeader(File* output, uint32_t frameCount) {
  if (frameCount == 0 || frameCount > kMaximumFrameCount)
    quit("Hybrid archive frame count is outside the v1 resource limit.");

  std::array<uint8_t, kArchiveHeaderSize> encoded{};
  for (size_t i = 0; i < kArchiveMagic.size(); ++i)
    encoded[i] = kArchiveMagic[i];
  store16(encoded.data() + 8, kArchiveMajorVersion);
  store16(encoded.data() + 10, kArchiveMinorVersion);
  store16(encoded.data() + 12, kArchiveHeaderSize);
  store16(encoded.data() + 14, 0);
  store32(encoded.data() + 16, frameCount);
  store32(encoded.data() + 20, 0);
  store32(encoded.data() + 20, crc32(encoded.data(), encoded.size()));
  writeExact(output, encoded.data(), encoded.size());
}

inline ArchiveHeader readArchiveHeader(File* input) {
  std::array<uint8_t, kArchiveHeaderSize> encoded{};
  readExact(input, encoded.data(), encoded.size(), "Truncated hybrid archive header.");
  for (size_t i = 0; i < kArchiveMagic.size(); ++i) {
    if (encoded[i] != kArchiveMagic[i])
      quit("Invalid hybrid archive magic.");
  }

  const uint32_t archivedCrc = load32(encoded.data() + 20);
  store32(encoded.data() + 20, 0);
  if (crc32(encoded.data(), encoded.size()) != archivedCrc)
    quit("Hybrid archive header checksum mismatch.");
  if (load16(encoded.data() + 8) != kArchiveMajorVersion)
    quit("Unsupported hybrid archive major version.");
  if (load16(encoded.data() + 10) > kArchiveMinorVersion)
    quit("Unsupported hybrid archive minor version.");
  if (load16(encoded.data() + 12) != kArchiveHeaderSize)
    quit("Unsupported hybrid archive header size.");
  if (load16(encoded.data() + 14) != 0)
    quit("Unsupported hybrid archive feature flags.");

  ArchiveHeader header;
  header.majorVersion = load16(encoded.data() + 8);
  header.minorVersion = load16(encoded.data() + 10);
  header.flags = load16(encoded.data() + 14);
  header.frameCount = load32(encoded.data() + 16);
  if (header.frameCount == 0 || header.frameCount > kMaximumFrameCount)
    quit("Hybrid archive frame count is outside the v1 resource limit.");
  return header;
}

inline void writeFrameHeader(File* output, const FrameHeader& header) {
  if (header.decodedLength > kMaximumFrameDataLength ||
      header.payloadLength > kMaximumFrameDataLength ||
      header.recipeLength > kMaximumFrameDataLength ||
      header.payloadLength > kMaximumFrameDataLength - header.recipeLength)
    quit("Hybrid frame length is outside the v1 resource limit.");

  std::array<uint8_t, kFrameHeaderSize> encoded{};
  for (size_t i = 0; i < kFrameMarker.size(); ++i)
    encoded[i] = kFrameMarker[i];
  store16(encoded.data() + 4, kFrameHeaderVersion);
  store16(encoded.data() + 6, kFrameHeaderSize);
  store16(encoded.data() + 8, static_cast<uint16_t>(header.route));
  store16(encoded.data() + 10, header.flags);
  store32(encoded.data() + 12, static_cast<uint32_t>(header.decoderContract));
  store64(encoded.data() + 16, header.decodedLength);
  store64(encoded.data() + 24, header.payloadLength);
  store64(encoded.data() + 32, header.recipeLength);
  store32(encoded.data() + 40, header.payloadCrc32);
  store32(encoded.data() + 44, header.decodedCrc32);
  store32(encoded.data() + 48, header.recipeCrc32);
  store32(encoded.data() + 52, 0);
  store64(encoded.data() + 56, 0);
  store32(encoded.data() + 52, crc32(encoded.data(), encoded.size()));
  writeExact(output, encoded.data(), encoded.size());
}

inline FrameHeader readFrameHeader(File* input) {
  std::array<uint8_t, kFrameHeaderSize> encoded{};
  readExact(input, encoded.data(), encoded.size(), "Truncated hybrid frame header.");
  for (size_t i = 0; i < kFrameMarker.size(); ++i) {
    if (encoded[i] != kFrameMarker[i])
      quit("Invalid hybrid frame marker.");
  }

  const uint32_t archivedCrc = load32(encoded.data() + 52);
  store32(encoded.data() + 52, 0);
  if (crc32(encoded.data(), encoded.size()) != archivedCrc)
    quit("Hybrid frame header checksum mismatch.");
  if (load16(encoded.data() + 4) != kFrameHeaderVersion)
    quit("Unsupported hybrid frame version.");
  if (load16(encoded.data() + 6) != kFrameHeaderSize)
    quit("Unsupported hybrid frame header size.");
  if (load16(encoded.data() + 10) != 0)
    quit("Unsupported hybrid frame flags.");
  if (load64(encoded.data() + 56) != 0)
    quit("Hybrid frame reserved field is not zero.");

  FrameHeader header;
  header.route = static_cast<RouteId>(load16(encoded.data() + 8));
  header.flags = load16(encoded.data() + 10);
  header.decoderContract = static_cast<DecoderContractId>(load32(encoded.data() + 12));
  header.decodedLength = load64(encoded.data() + 16);
  header.payloadLength = load64(encoded.data() + 24);
  header.recipeLength = load64(encoded.data() + 32);
  header.payloadCrc32 = load32(encoded.data() + 40);
  header.decodedCrc32 = load32(encoded.data() + 44);
  header.recipeCrc32 = load32(encoded.data() + 48);
  if (header.decodedLength > kMaximumFrameDataLength ||
      header.payloadLength > kMaximumFrameDataLength ||
      header.recipeLength > kMaximumFrameDataLength ||
      header.payloadLength > kMaximumFrameDataLength - header.recipeLength)
    quit("Hybrid frame length is outside the v1 resource limit.");
  return header;
}

inline void requireEndOfArchive(File* input) {
  if (input == nullptr || input->getchar() != EOF)
    quit("Unexpected trailing data after hybrid archive frames.");
}

} // namespace hybrid
