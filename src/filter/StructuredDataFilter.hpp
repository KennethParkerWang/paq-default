#pragma once

#include "../Array.hpp"
#include "../BlockType.hpp"
#include "../Utils.hpp"
#include "Filter.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Reversible preprocessing for statistically detected structured DEFAULT data.
//
// Archive invariants:
//   * every mapping preserves the byte count;
//   * encoder and decoder use only BlockType, blockInfo and decoded block size;
//   * memory use is bounded by two fixed 64 KiB tiles plus one 64 KiB row;
//   * no encode -> inverse -> compare probe is needed for correctness.
namespace structured {

constexpr size_t kStructuredTileBytes = size_t{64} * 1024;

enum class RecordTransform : uint8_t {
  MODEL_ONLY = 0,
  TRANSPOSE = 1,
  TRANSPOSE_DELTA = 2,
};

enum class NumericTransform : uint8_t {
  BYTE_SHUFFLE = 1,
  VERTICAL = 2,
  LORENZO = 3,
  LORENZO_SHUFFLE = 4,
};

struct RecordInfo {
  uint16_t stride{};
  RecordTransform transform{RecordTransform::MODEL_ONLY};
};

struct NumericInfo {
  uint16_t rowWidthElements{};
  uint8_t elementBytes{};
  bool bigEndian{};
  NumericTransform transform{NumericTransform::BYTE_SHUFFLE};
};

struct WideTextInfo {
  uint8_t elementBytes{};
};

// RECORD blockInfo:
//   bits  0..11: record stride in bytes (2..4095)
//   bits 12..13: RecordTransform
//   bits 14..31: zero
constexpr uint32_t kRecordStrideMask = 0x00000FFFu;
constexpr uint32_t kRecordTransformMask = 0x00003000u;
constexpr uint32_t kRecordReservedMask = 0xFFFFC000u;

inline bool isValidRecordInfo(const uint32_t info) {
  const uint32_t stride = info & kRecordStrideMask;
  const uint32_t transform = (info & kRecordTransformMask) >> 12;
  return (info & kRecordReservedMask) == 0 && stride >= 2 && stride <= 4095 &&
         transform <= static_cast<uint32_t>(RecordTransform::TRANSPOSE_DELTA);
}

inline void validateRecordInfo(const uint32_t info) {
  if (!isValidRecordInfo(info)) {
    quit("Corrupted RECORD block metadata.");
  }
}

inline uint32_t packRecordInfo(const uint16_t stride, const RecordTransform transform) {
  if (stride < 2 || stride > 4095 ||
      static_cast<uint32_t>(transform) >
          static_cast<uint32_t>(RecordTransform::TRANSPOSE_DELTA)) {
    quit("Invalid RECORD metadata to pack.");
  }
  const uint32_t info = static_cast<uint32_t>(stride) |
                        (static_cast<uint32_t>(transform) << 12);
  validateRecordInfo(info);
  return info;
}

inline RecordInfo unpackRecordInfo(const uint32_t info) {
  validateRecordInfo(info);
  RecordInfo result;
  result.stride = static_cast<uint16_t>(info & kRecordStrideMask);
  result.transform = static_cast<RecordTransform>((info & kRecordTransformMask) >> 12);
  return result;
}

inline uint16_t unpackRecordStride(const uint32_t info) {
  return unpackRecordInfo(info).stride;
}

inline RecordTransform unpackRecordTransform(const uint32_t info) {
  return unpackRecordInfo(info).transform;
}

inline bool recordInfoHasTransform(const uint32_t info) {
  return unpackRecordTransform(info) != RecordTransform::MODEL_ONLY;
}

// NUMERIC blockInfo:
//   bits  0..15: row width in elements (0 is allowed only for BYTE_SHUFFLE)
//   bits 16..17: log2(elementBytes), encoding 1/2/4/8 bytes as 0/1/2/3
//   bit      18: big-endian element representation
//   bits 19..21: NumericTransform (1..4)
//   bits 22..31: zero
constexpr uint32_t kNumericRowWidthMask = 0x0000FFFFu;
constexpr uint32_t kNumericElementCodeMask = 0x00030000u;
constexpr uint32_t kNumericBigEndianMask = 0x00040000u;
constexpr uint32_t kNumericTransformMask = 0x00380000u;
constexpr uint32_t kNumericReservedMask = 0xFFC00000u;

inline uint8_t numericElementBytesFromInfoUnchecked(const uint32_t info) {
  return static_cast<uint8_t>(1u << ((info & kNumericElementCodeMask) >> 16));
}

inline bool isNumericPredictor(const NumericTransform transform) {
  return transform == NumericTransform::VERTICAL ||
         transform == NumericTransform::LORENZO ||
         transform == NumericTransform::LORENZO_SHUFFLE;
}

inline bool isValidNumericInfo(const uint32_t info) {
  if ((info & kNumericReservedMask) != 0) {
    return false;
  }

  const uint32_t modeCode = (info & kNumericTransformMask) >> 19;
  if (modeCode < static_cast<uint32_t>(NumericTransform::BYTE_SHUFFLE) ||
      modeCode > static_cast<uint32_t>(NumericTransform::LORENZO_SHUFFLE)) {
    return false;
  }

  const uint32_t width = info & kNumericRowWidthMask;
  const uint32_t elementBytes = numericElementBytesFromInfoUnchecked(info);
  const bool bigEndian = (info & kNumericBigEndianMask) != 0;
  const auto transform = static_cast<NumericTransform>(modeCode);

  if (elementBytes == 1 && bigEndian) {
    return false;
  }
  if (width != 0 && width * elementBytes > kStructuredTileBytes) {
    return false;
  }
  if (isNumericPredictor(transform) &&
      (width == 0 || (elementBytes != 1 && elementBytes != 2))) {
    return false;
  }
  return true;
}

inline void validateNumericInfo(const uint32_t info) {
  if (!isValidNumericInfo(info)) {
    quit("Corrupted NUMERIC block metadata.");
  }
}

inline uint8_t numericElementCode(const uint8_t elementBytes) {
  switch (elementBytes) {
    case 1: return 0;
    case 2: return 1;
    case 4: return 2;
    case 8: return 3;
    default: quit("Invalid NUMERIC element size.");
  }
}

inline uint32_t packNumericInfo(const uint16_t rowWidthElements,
                                const uint8_t elementBytes,
                                const bool bigEndian,
                                const NumericTransform transform) {
  const uint32_t info = static_cast<uint32_t>(rowWidthElements) |
                        (static_cast<uint32_t>(numericElementCode(elementBytes)) << 16) |
                        (static_cast<uint32_t>(bigEndian) << 18) |
                        (static_cast<uint32_t>(transform) << 19);
  validateNumericInfo(info);
  return info;
}

inline NumericInfo unpackNumericInfo(const uint32_t info) {
  validateNumericInfo(info);
  NumericInfo result;
  result.rowWidthElements = static_cast<uint16_t>(info & kNumericRowWidthMask);
  result.elementBytes = numericElementBytesFromInfoUnchecked(info);
  result.bigEndian = (info & kNumericBigEndianMask) != 0;
  result.transform = static_cast<NumericTransform>((info & kNumericTransformMask) >> 19);
  return result;
}

inline uint16_t unpackNumericRowWidth(const uint32_t info) {
  return unpackNumericInfo(info).rowWidthElements;
}

inline uint8_t unpackNumericElementBytes(const uint32_t info) {
  return unpackNumericInfo(info).elementBytes;
}

inline bool unpackNumericBigEndian(const uint32_t info) {
  return unpackNumericInfo(info).bigEndian;
}

inline NumericTransform unpackNumericTransform(const uint32_t info) {
  return unpackNumericInfo(info).transform;
}

// WIDE_TEXT blockInfo:
//   bits 0..2 : elementBytes stored as its actual value (2 or 4)
//   bits 3..31: zero
constexpr uint32_t kWideTextElementBytesMask = 0x00000007u;
constexpr uint32_t kWideTextReservedMask = 0xFFFFFFF8u;

inline bool isValidWideTextInfo(const uint32_t info) {
  const uint32_t elementBytes = info & kWideTextElementBytesMask;
  return (info & kWideTextReservedMask) == 0 &&
         (elementBytes == 2 || elementBytes == 4);
}

inline void validateWideTextInfo(const uint32_t info) {
  if (!isValidWideTextInfo(info)) {
    quit("Corrupted WIDE_TEXT block metadata.");
  }
}

inline uint32_t packWideTextInfo(const uint8_t elementBytes) {
  const uint32_t info = elementBytes;
  validateWideTextInfo(info);
  return info;
}

inline WideTextInfo unpackWideTextInfo(const uint32_t info) {
  validateWideTextInfo(info);
  WideTextInfo result;
  result.elementBytes = static_cast<uint8_t>(info & kWideTextElementBytesMask);
  return result;
}

inline uint8_t unpackWideTextElementBytes(const uint32_t info) {
  return unpackWideTextInfo(info).elementBytes;
}

inline bool isStructuredType(const BlockType type) {
  return type == BlockType::RECORD || type == BlockType::NUMERIC ||
         type == BlockType::WIDE_TEXT;
}

inline bool isValidStructuredInfo(const BlockType type, const uint32_t info) {
  if (type == BlockType::RECORD) {
    return isValidRecordInfo(info);
  }
  if (type == BlockType::NUMERIC) {
    return isValidNumericInfo(info);
  }
  if (type == BlockType::WIDE_TEXT) {
    return isValidWideTextInfo(info);
  }
  return false;
}

inline void validateStructuredInfo(const BlockType type, const uint32_t info) {
  if (type == BlockType::RECORD) {
    validateRecordInfo(info);
  }
  else if (type == BlockType::NUMERIC) {
    validateNumericInfo(info);
  }
  else if (type == BlockType::WIDE_TEXT) {
    validateWideTextInfo(info);
  }
  else {
    quit("Invalid structured filter block type.");
  }
}

// The following buffer primitives operate on one independently reversible tile.
// Source and destination must be distinct for transpose and shuffle operations.
// Any final incomplete record/element is copied byte-for-byte.
inline void recordTransposeForward(const uint8_t* const source,
                                   uint8_t* const destination,
                                   const size_t bytes,
                                   const size_t stride) {
  if (stride < 2 || stride > 4095) {
    quit("Invalid RECORD transpose stride.");
  }
  const size_t records = bytes / stride;
  const size_t completeBytes = records * stride;
  for (size_t lane = 0; lane < stride; ++lane) {
    for (size_t record = 0; record < records; ++record) {
      destination[lane * records + record] = source[record * stride + lane];
    }
  }
  if (completeBytes != bytes) {
    std::memcpy(destination + completeBytes, source + completeBytes,
                bytes - completeBytes);
  }
}

inline void recordTransposeInverse(const uint8_t* const source,
                                   uint8_t* const destination,
                                   const size_t bytes,
                                   const size_t stride) {
  if (stride < 2 || stride > 4095) {
    quit("Invalid RECORD transpose stride.");
  }
  const size_t records = bytes / stride;
  const size_t completeBytes = records * stride;
  for (size_t lane = 0; lane < stride; ++lane) {
    for (size_t record = 0; record < records; ++record) {
      destination[record * stride + lane] = source[lane * records + record];
    }
  }
  if (completeBytes != bytes) {
    std::memcpy(destination + completeBytes, source + completeBytes,
                bytes - completeBytes);
  }
}

inline void recordTransposeDeltaForward(const uint8_t* const source,
                                        uint8_t* const destination,
                                        const size_t bytes,
                                        const size_t stride) {
  if (stride < 2 || stride > 4095) {
    quit("Invalid RECORD transpose-delta stride.");
  }
  const size_t records = bytes / stride;
  const size_t completeBytes = records * stride;
  for (size_t lane = 0; lane < stride; ++lane) {
    uint8_t previous = 0;
    for (size_t record = 0; record < records; ++record) {
      const uint8_t current = source[record * stride + lane];
      destination[lane * records + record] =
          record == 0 ? current : static_cast<uint8_t>(current - previous);
      previous = current;
    }
  }
  if (completeBytes != bytes) {
    std::memcpy(destination + completeBytes, source + completeBytes,
                bytes - completeBytes);
  }
}

inline void recordTransposeDeltaInverse(const uint8_t* const source,
                                        uint8_t* const destination,
                                        const size_t bytes,
                                        const size_t stride) {
  if (stride < 2 || stride > 4095) {
    quit("Invalid RECORD transpose-delta stride.");
  }
  const size_t records = bytes / stride;
  const size_t completeBytes = records * stride;
  for (size_t lane = 0; lane < stride; ++lane) {
    uint8_t previous = 0;
    for (size_t record = 0; record < records; ++record) {
      const uint8_t encoded = source[lane * records + record];
      const uint8_t current =
          record == 0 ? encoded : static_cast<uint8_t>(previous + encoded);
      destination[record * stride + lane] = current;
      previous = current;
    }
  }
  if (completeBytes != bytes) {
    std::memcpy(destination + completeBytes, source + completeBytes,
                bytes - completeBytes);
  }
}

inline void byteShuffleForward(const uint8_t* const source,
                               uint8_t* const destination,
                               const size_t bytes,
                               const size_t elementBytes) {
  if (elementBytes != 1 && elementBytes != 2 && elementBytes != 4 &&
      elementBytes != 8) {
    quit("Invalid byte-shuffle element size.");
  }
  const size_t elements = bytes / elementBytes;
  const size_t completeBytes = elements * elementBytes;
  for (size_t lane = 0; lane < elementBytes; ++lane) {
    for (size_t element = 0; element < elements; ++element) {
      destination[lane * elements + element] =
          source[element * elementBytes + lane];
    }
  }
  if (completeBytes != bytes) {
    std::memcpy(destination + completeBytes, source + completeBytes,
                bytes - completeBytes);
  }
}

inline void byteShuffleInverse(const uint8_t* const source,
                               uint8_t* const destination,
                               const size_t bytes,
                               const size_t elementBytes) {
  if (elementBytes != 1 && elementBytes != 2 && elementBytes != 4 &&
      elementBytes != 8) {
    quit("Invalid byte-shuffle element size.");
  }
  const size_t elements = bytes / elementBytes;
  const size_t completeBytes = elements * elementBytes;
  for (size_t lane = 0; lane < elementBytes; ++lane) {
    for (size_t element = 0; element < elements; ++element) {
      destination[element * elementBytes + lane] =
          source[lane * elements + element];
    }
  }
  if (completeBytes != bytes) {
    std::memcpy(destination + completeBytes, source + completeBytes,
                bytes - completeBytes);
  }
}

namespace detail {

enum class Predictor : uint8_t {
  VERTICAL,
  LORENZO,
};

inline uint16_t readElement(const uint8_t* const source,
                            const uint8_t elementBytes,
                            const bool bigEndian) {
  if (elementBytes == 1) {
    return source[0];
  }
  return bigEndian
             ? static_cast<uint16_t>((static_cast<uint16_t>(source[0]) << 8) |
                                     source[1])
             : static_cast<uint16_t>(source[0] |
                                     (static_cast<uint16_t>(source[1]) << 8));
}

inline void writeElement(uint8_t* const destination,
                         const uint16_t value,
                         const uint8_t elementBytes,
                         const bool bigEndian) {
  if (elementBytes == 1) {
    destination[0] = static_cast<uint8_t>(value);
  }
  else if (bigEndian) {
    destination[0] = static_cast<uint8_t>(value >> 8);
    destination[1] = static_cast<uint8_t>(value);
  }
  else {
    destination[0] = static_cast<uint8_t>(value);
    destination[1] = static_cast<uint8_t>(value >> 8);
  }
}

// ASSUMPTION: predictor history continues across tile boundaries. Tiles are an
// I/O/memory boundary, not a model boundary. This preserves the two-dimensional
// coordinate system while the final incomplete row remains untouched.
inline void predictForwardRows(const uint8_t* const source,
                               uint8_t* const destination,
                               const size_t rows,
                               const uint16_t width,
                               const uint8_t elementBytes,
                               const bool bigEndian,
                               const Predictor predictor,
                               Array<uint8_t>& previousRow,
                               bool& hasPreviousRow) {
  const uint32_t mask = elementBytes == 1 ? 0xFFu : 0xFFFFu;
  const size_t rowBytes = static_cast<size_t>(width) * elementBytes;
  for (size_t row = 0; row < rows; ++row) {
    uint16_t left = 0;
    uint16_t oldUpLeft = 0;
    for (size_t x = 0; x < width; ++x) {
      const size_t offset = row * rowBytes + x * elementBytes;
      const uint16_t current = readElement(source + offset, elementBytes, bigEndian);
      const size_t previousOffset = x * elementBytes;
      const uint16_t up =
          readElement(&previousRow[previousOffset], elementBytes, bigEndian);
      uint32_t prediction = 0;
      if (predictor == Predictor::VERTICAL) {
        prediction = hasPreviousRow ? up : 0;
      }
      else if (!hasPreviousRow) {
        prediction = x == 0 ? 0 : left;
      }
      else {
        prediction = x == 0
                         ? up
                         : (static_cast<uint32_t>(left) + up - oldUpLeft) & mask;
      }
      const uint16_t residual =
          static_cast<uint16_t>((static_cast<uint32_t>(current) - prediction) & mask);
      writeElement(destination + offset, residual, elementBytes, bigEndian);

      writeElement(&previousRow[previousOffset], current, elementBytes,
                   bigEndian);
      oldUpLeft = up;
      left = current;
    }
    hasPreviousRow = true;
  }
}

inline void predictInverseRows(const uint8_t* const source,
                               uint8_t* const destination,
                               const size_t rows,
                               const uint16_t width,
                               const uint8_t elementBytes,
                               const bool bigEndian,
                               const Predictor predictor,
                               Array<uint8_t>& previousRow,
                               bool& hasPreviousRow) {
  const uint32_t mask = elementBytes == 1 ? 0xFFu : 0xFFFFu;
  const size_t rowBytes = static_cast<size_t>(width) * elementBytes;
  for (size_t row = 0; row < rows; ++row) {
    uint16_t left = 0;
    uint16_t oldUpLeft = 0;
    for (size_t x = 0; x < width; ++x) {
      const size_t offset = row * rowBytes + x * elementBytes;
      const uint16_t residual = readElement(source + offset, elementBytes, bigEndian);
      const size_t previousOffset = x * elementBytes;
      const uint16_t up =
          readElement(&previousRow[previousOffset], elementBytes, bigEndian);
      uint32_t prediction = 0;
      if (predictor == Predictor::VERTICAL) {
        prediction = hasPreviousRow ? up : 0;
      }
      else if (!hasPreviousRow) {
        prediction = x == 0 ? 0 : left;
      }
      else {
        prediction = x == 0
                         ? up
                         : (static_cast<uint32_t>(left) + up - oldUpLeft) & mask;
      }
      const uint16_t current =
          static_cast<uint16_t>((static_cast<uint32_t>(residual) + prediction) & mask);
      writeElement(destination + offset, current, elementBytes, bigEndian);

      writeElement(&previousRow[previousOffset], current, elementBytes,
                   bigEndian);
      oldUpLeft = up;
      left = current;
    }
    hasPreviousRow = true;
  }
}

inline void validatePredictorArguments(const size_t width,
                                       const uint8_t elementBytes,
                                       const bool bigEndian) {
  if (width == 0 || width > 65535 ||
      (elementBytes != 1 && elementBytes != 2) ||
      (elementBytes == 1 && bigEndian) ||
      width * elementBytes > kStructuredTileBytes) {
    quit("Invalid NUMERIC predictor geometry.");
  }
}

inline void predictForward(const uint8_t* const source,
                           uint8_t* const destination,
                           const size_t bytes,
                           const size_t width,
                           const uint8_t elementBytes,
                           const bool bigEndian,
                           const Predictor predictor) {
  validatePredictorArguments(width, elementBytes, bigEndian);
  const size_t rowBytes = width * elementBytes;
  const size_t rows = bytes / rowBytes;
  const size_t completeBytes = rows * rowBytes;
  Array<uint8_t> previousRow(rowBytes);
  bool hasPreviousRow = false;
  predictForwardRows(source, destination, rows, static_cast<uint16_t>(width),
                     elementBytes, bigEndian, predictor, previousRow,
                     hasPreviousRow);
  if (completeBytes != bytes) {
    std::memcpy(destination + completeBytes, source + completeBytes,
                bytes - completeBytes);
  }
}

inline void predictInverse(const uint8_t* const source,
                           uint8_t* const destination,
                           const size_t bytes,
                           const size_t width,
                           const uint8_t elementBytes,
                           const bool bigEndian,
                           const Predictor predictor) {
  validatePredictorArguments(width, elementBytes, bigEndian);
  const size_t rowBytes = width * elementBytes;
  const size_t rows = bytes / rowBytes;
  const size_t completeBytes = rows * rowBytes;
  Array<uint8_t> previousRow(rowBytes);
  bool hasPreviousRow = false;
  predictInverseRows(source, destination, rows, static_cast<uint16_t>(width),
                     elementBytes, bigEndian, predictor, previousRow,
                     hasPreviousRow);
  if (completeBytes != bytes) {
    std::memcpy(destination + completeBytes, source + completeBytes,
                bytes - completeBytes);
  }
}

} // namespace detail

inline void vertical8Forward(const uint8_t* const source,
                             uint8_t* const destination,
                             const size_t bytes,
                             const size_t width) {
  detail::predictForward(source, destination, bytes, width, 1, false,
                         detail::Predictor::VERTICAL);
}

inline void vertical8Inverse(const uint8_t* const source,
                             uint8_t* const destination,
                             const size_t bytes,
                             const size_t width) {
  detail::predictInverse(source, destination, bytes, width, 1, false,
                         detail::Predictor::VERTICAL);
}

inline void vertical16Forward(const uint8_t* const source,
                              uint8_t* const destination,
                              const size_t bytes,
                              const size_t width,
                              const bool bigEndian) {
  detail::predictForward(source, destination, bytes, width, 2, bigEndian,
                         detail::Predictor::VERTICAL);
}

inline void vertical16Inverse(const uint8_t* const source,
                              uint8_t* const destination,
                              const size_t bytes,
                              const size_t width,
                              const bool bigEndian) {
  detail::predictInverse(source, destination, bytes, width, 2, bigEndian,
                         detail::Predictor::VERTICAL);
}

inline void lorenzo8Forward(const uint8_t* const source,
                            uint8_t* const destination,
                            const size_t bytes,
                            const size_t width) {
  detail::predictForward(source, destination, bytes, width, 1, false,
                         detail::Predictor::LORENZO);
}

inline void lorenzo8Inverse(const uint8_t* const source,
                            uint8_t* const destination,
                            const size_t bytes,
                            const size_t width) {
  detail::predictInverse(source, destination, bytes, width, 1, false,
                         detail::Predictor::LORENZO);
}

inline void lorenzo16Forward(const uint8_t* const source,
                             uint8_t* const destination,
                             const size_t bytes,
                             const size_t width,
                             const bool bigEndian) {
  detail::predictForward(source, destination, bytes, width, 2, bigEndian,
                         detail::Predictor::LORENZO);
}

inline void lorenzo16Inverse(const uint8_t* const source,
                             uint8_t* const destination,
                             const size_t bytes,
                             const size_t width,
                             const bool bigEndian) {
  detail::predictInverse(source, destination, bytes, width, 2, bigEndian,
                         detail::Predictor::LORENZO);
}

} // namespace structured

class StructuredDataFilter final : public Filter {
private:
  BlockType type;
  uint64_t decodedOutputBase{};
  uint32_t explicitPackedInfo{};
  bool hasExplicitPackedInfo{};

  static void readExact(File* const input,
                        uint8_t* const destination,
                        const size_t bytes) {
    if (bytes != 0 && input->blockRead(destination, bytes) != bytes) {
      quit("Unexpected end of structured transform input.");
    }
  }

  static void writeBytes(File* const output,
                         uint8_t* const source,
                         const size_t bytes) {
    if (bytes != 0) {
      output->blockWrite(source, bytes);
    }
  }

  void readEncoded(uint8_t* const destination, const size_t bytes) const {
    for (size_t i = 0; i < bytes; ++i) {
      destination[i] = encoder->decompressByte(encoder->predictorMain);
    }
  }

  void emitDecoded(File* const output,
                   const FMode mode,
                   uint8_t* const source,
                   const size_t bytes,
                   const uint64_t outputOffset,
                   uint64_t& diffFound) const {
    if (mode == FMode::FDECOMPRESS) {
      writeBytes(output, source, bytes);
      encoder->printStatus();
    }
    else if (mode == FMode::FCOMPARE) {
      for (size_t i = 0; i < bytes; ++i) {
        const int expected = output->getchar();
        if (diffFound == 0 && expected != source[i]) {
          diffFound = decodedOutputBase + outputOffset + i + 1;
        }
      }
    }
    else if (mode != FMode::FDISCARD) {
      quit("Invalid structured filter output mode.");
    }
  }

  static size_t unitAlignedTileBytes(const size_t unitBytes) {
    if (unitBytes == 0 || unitBytes > structured::kStructuredTileBytes) {
      quit("Invalid structured transform unit size.");
    }
    return (structured::kStructuredTileBytes / unitBytes) * unitBytes;
  }

  static void encodePassThrough(File* const input,
                                File* const output,
                                const uint64_t size) {
    Array<uint8_t> buffer(structured::kStructuredTileBytes);
    uint64_t remaining = size;
    while (remaining != 0) {
      const size_t chunk = static_cast<size_t>(
          std::min<uint64_t>(remaining, structured::kStructuredTileBytes));
      readExact(input, &buffer[0], chunk);
      writeBytes(output, &buffer[0], chunk);
      remaining -= chunk;
    }
  }

  uint64_t decodePassThrough(File* const output,
                             const FMode mode,
                             const uint64_t size,
                             uint64_t& diffFound) {
    Array<uint8_t> buffer(structured::kStructuredTileBytes);
    uint64_t offset = 0;
    while (offset < size) {
      const size_t chunk = static_cast<size_t>(std::min<uint64_t>(
          size - offset, structured::kStructuredTileBytes));
      readEncoded(&buffer[0], chunk);
      emitDecoded(output, mode, &buffer[0], chunk, offset, diffFound);
      offset += chunk;
    }
    return size;
  }

  static void encodeRecord(File* const input,
                           File* const output,
                           const uint64_t size,
                           const structured::RecordInfo& info) {
    if (info.transform == structured::RecordTransform::MODEL_ONLY) {
      encodePassThrough(input, output, size);
      return;
    }

    Array<uint8_t> source(structured::kStructuredTileBytes);
    Array<uint8_t> transformed(structured::kStructuredTileBytes);
    const size_t tileBytes = unitAlignedTileBytes(info.stride);
    const uint64_t completeBytes = (size / info.stride) * info.stride;
    uint64_t offset = 0;
    while (offset < completeBytes) {
      const size_t chunk = static_cast<size_t>(
          std::min<uint64_t>(completeBytes - offset, tileBytes));
      readExact(input, &source[0], chunk);
      if (info.transform == structured::RecordTransform::TRANSPOSE) {
        structured::recordTransposeForward(&source[0], &transformed[0], chunk,
                                           info.stride);
      }
      else {
        structured::recordTransposeDeltaForward(&source[0], &transformed[0],
                                                chunk, info.stride);
      }
      writeBytes(output, &transformed[0], chunk);
      offset += chunk;
    }

    const size_t tail = static_cast<size_t>(size - completeBytes);
    if (tail != 0) {
      readExact(input, &source[0], tail);
      writeBytes(output, &source[0], tail);
    }
  }

  uint64_t decodeRecord(File* const output,
                        const FMode mode,
                        const uint64_t size,
                        uint64_t& diffFound,
                        const structured::RecordInfo& info) {
    if (info.transform == structured::RecordTransform::MODEL_ONLY) {
      return decodePassThrough(output, mode, size, diffFound);
    }

    Array<uint8_t> source(structured::kStructuredTileBytes);
    Array<uint8_t> restored(structured::kStructuredTileBytes);
    const size_t tileBytes = unitAlignedTileBytes(info.stride);
    const uint64_t completeBytes = (size / info.stride) * info.stride;
    uint64_t offset = 0;
    while (offset < completeBytes) {
      const size_t chunk = static_cast<size_t>(
          std::min<uint64_t>(completeBytes - offset, tileBytes));
      readEncoded(&source[0], chunk);
      if (info.transform == structured::RecordTransform::TRANSPOSE) {
        structured::recordTransposeInverse(&source[0], &restored[0], chunk,
                                           info.stride);
      }
      else {
        structured::recordTransposeDeltaInverse(&source[0], &restored[0],
                                                chunk, info.stride);
      }
      emitDecoded(output, mode, &restored[0], chunk, offset, diffFound);
      offset += chunk;
    }

    const size_t tail = static_cast<size_t>(size - completeBytes);
    if (tail != 0) {
      readEncoded(&source[0], tail);
      emitDecoded(output, mode, &source[0], tail, offset, diffFound);
    }
    return size;
  }

  static void encodeShuffle(File* const input,
                            File* const output,
                            const uint64_t size,
                            const uint8_t elementBytes) {
    Array<uint8_t> source(structured::kStructuredTileBytes);
    Array<uint8_t> transformed(structured::kStructuredTileBytes);
    const size_t tileBytes = unitAlignedTileBytes(elementBytes);
    const uint64_t completeBytes = (size / elementBytes) * elementBytes;
    uint64_t offset = 0;
    while (offset < completeBytes) {
      const size_t chunk = static_cast<size_t>(
          std::min<uint64_t>(completeBytes - offset, tileBytes));
      readExact(input, &source[0], chunk);
      structured::byteShuffleForward(&source[0], &transformed[0], chunk,
                                     elementBytes);
      writeBytes(output, &transformed[0], chunk);
      offset += chunk;
    }

    const size_t tail = static_cast<size_t>(size - completeBytes);
    if (tail != 0) {
      readExact(input, &source[0], tail);
      writeBytes(output, &source[0], tail);
    }
  }

  uint64_t decodeShuffle(File* const output,
                         const FMode mode,
                         const uint64_t size,
                         uint64_t& diffFound,
                         const uint8_t elementBytes) {
    Array<uint8_t> source(structured::kStructuredTileBytes);
    Array<uint8_t> restored(structured::kStructuredTileBytes);
    const size_t tileBytes = unitAlignedTileBytes(elementBytes);
    const uint64_t completeBytes = (size / elementBytes) * elementBytes;
    uint64_t offset = 0;
    while (offset < completeBytes) {
      const size_t chunk = static_cast<size_t>(
          std::min<uint64_t>(completeBytes - offset, tileBytes));
      readEncoded(&source[0], chunk);
      structured::byteShuffleInverse(&source[0], &restored[0], chunk,
                                     elementBytes);
      emitDecoded(output, mode, &restored[0], chunk, offset, diffFound);
      offset += chunk;
    }

    const size_t tail = static_cast<size_t>(size - completeBytes);
    if (tail != 0) {
      readEncoded(&source[0], tail);
      emitDecoded(output, mode, &source[0], tail, offset, diffFound);
    }
    return size;
  }

  static structured::detail::Predictor predictorFor(
      const structured::NumericTransform transform) {
    return transform == structured::NumericTransform::VERTICAL
               ? structured::detail::Predictor::VERTICAL
               : structured::detail::Predictor::LORENZO;
  }

  static void encodeNumericPredictor(File* const input,
                                     File* const output,
                                     const uint64_t size,
                                     const structured::NumericInfo& info) {
    Array<uint8_t> source(structured::kStructuredTileBytes);
    Array<uint8_t> transformed(structured::kStructuredTileBytes);
    const size_t rowBytes =
        static_cast<size_t>(info.rowWidthElements) * info.elementBytes;
    Array<uint8_t> previousRow(rowBytes);
    bool hasPreviousRow = false;
    const size_t tileBytes = unitAlignedTileBytes(rowBytes);
    const uint64_t completeBytes = (size / rowBytes) * rowBytes;
    const bool shuffle =
        info.transform == structured::NumericTransform::LORENZO_SHUFFLE;
    const auto predictor = predictorFor(info.transform);

    uint64_t offset = 0;
    while (offset < completeBytes) {
      const size_t chunk = static_cast<size_t>(
          std::min<uint64_t>(completeBytes - offset, tileBytes));
      readExact(input, &source[0], chunk);
      structured::detail::predictForwardRows(
          &source[0], &transformed[0], chunk / rowBytes,
          info.rowWidthElements, info.elementBytes, info.bigEndian, predictor,
          previousRow, hasPreviousRow);
      if (shuffle) {
        structured::byteShuffleForward(&transformed[0], &source[0], chunk,
                                       info.elementBytes);
        writeBytes(output, &source[0], chunk);
      }
      else {
        writeBytes(output, &transformed[0], chunk);
      }
      offset += chunk;
    }

    // A partial final row, including any partial element, is outside the
    // archived geometry and therefore remains byte-identical.
    uint64_t tail = size - completeBytes;
    while (tail != 0) {
      const size_t chunk = static_cast<size_t>(
          std::min<uint64_t>(tail, structured::kStructuredTileBytes));
      readExact(input, &source[0], chunk);
      writeBytes(output, &source[0], chunk);
      tail -= chunk;
    }
  }

  uint64_t decodeNumericPredictor(File* const output,
                                  const FMode mode,
                                  const uint64_t size,
                                  uint64_t& diffFound,
                                  const structured::NumericInfo& info) {
    Array<uint8_t> source(structured::kStructuredTileBytes);
    Array<uint8_t> workspace(structured::kStructuredTileBytes);
    const size_t rowBytes =
        static_cast<size_t>(info.rowWidthElements) * info.elementBytes;
    Array<uint8_t> previousRow(rowBytes);
    bool hasPreviousRow = false;
    const size_t tileBytes = unitAlignedTileBytes(rowBytes);
    const uint64_t completeBytes = (size / rowBytes) * rowBytes;
    const bool shuffle =
        info.transform == structured::NumericTransform::LORENZO_SHUFFLE;
    const auto predictor = predictorFor(info.transform);

    uint64_t offset = 0;
    while (offset < completeBytes) {
      const size_t chunk = static_cast<size_t>(
          std::min<uint64_t>(completeBytes - offset, tileBytes));
      readEncoded(&source[0], chunk);
      uint8_t* residual = &source[0];
      uint8_t* restored = &workspace[0];
      if (shuffle) {
        structured::byteShuffleInverse(&source[0], &workspace[0], chunk,
                                       info.elementBytes);
        residual = &workspace[0];
        restored = &source[0];
      }
      structured::detail::predictInverseRows(
          residual, restored, chunk / rowBytes, info.rowWidthElements,
          info.elementBytes, info.bigEndian, predictor, previousRow,
          hasPreviousRow);
      emitDecoded(output, mode, restored, chunk, offset, diffFound);
      offset += chunk;
    }

    uint64_t tail = size - completeBytes;
    while (tail != 0) {
      const size_t chunk = static_cast<size_t>(
          std::min<uint64_t>(tail, structured::kStructuredTileBytes));
      readEncoded(&source[0], chunk);
      emitDecoded(output, mode, &source[0], chunk, offset, diffFound);
      offset += chunk;
      tail -= chunk;
    }
    return size;
  }

public:
  explicit StructuredDataFilter(const BlockType blockType) : type(blockType) {
    if (!structured::isStructuredType(type)) {
      quit("Invalid StructuredDataFilter block type.");
    }
  }

  StructuredDataFilter(const BlockType blockType, const int info)
      : StructuredDataFilter(blockType) {
    setInfo(info);
  }

  void setInfo(const int info) {
    const uint32_t packedInfo = static_cast<uint32_t>(info);
    structured::validateStructuredInfo(type, packedInfo);
    explicitPackedInfo = packedInfo;
    hasExplicitPackedInfo = true;
  }

  void encode(File* const input,
              File* const output,
              const uint64_t size,
              const int info,
              int& /*headerSize*/) override {
    if (input == nullptr || output == nullptr) {
      quit("Null structured transform file.");
    }
    const uint32_t packedInfo = static_cast<uint32_t>(info);
    structured::validateStructuredInfo(type, packedInfo);
    if (hasExplicitPackedInfo && packedInfo != explicitPackedInfo) {
      quit("Structured transform metadata changed after construction.");
    }

    if (type == BlockType::RECORD) {
      encodeRecord(input, output, size,
                   structured::unpackRecordInfo(packedInfo));
    }
    else if (type == BlockType::NUMERIC) {
      const structured::NumericInfo numeric =
          structured::unpackNumericInfo(packedInfo);
      if (numeric.transform == structured::NumericTransform::BYTE_SHUFFLE) {
        encodeShuffle(input, output, size, numeric.elementBytes);
      }
      else {
        encodeNumericPredictor(input, output, size, numeric);
      }
    }
    else {
      encodeShuffle(input, output, size,
                    structured::unpackWideTextElementBytes(packedInfo));
    }
  }

  uint64_t decode(File* const /*input*/,
                  File* const output,
                  const FMode mode,
                  const uint64_t size,
                  uint64_t& diffFound) override {
    if (encoder == nullptr) {
      quit("StructuredDataFilter has no valid decoder.");
    }
    if (!hasExplicitPackedInfo &&
        encoder->getShared() == nullptr) {
      quit("StructuredDataFilter has no archived metadata source.");
    }
    const uint32_t packedInfo = hasExplicitPackedInfo
                                    ? explicitPackedInfo
                                    : static_cast<uint32_t>(
                                          encoder->getShared()->State.blockInfo);
    structured::validateStructuredInfo(type, packedInfo);
    if (mode != FMode::FDISCARD && output == nullptr) {
      quit("Null structured transform output.");
    }
    decodedOutputBase = mode == FMode::FCOMPARE ? output->curPos() : 0;

    // The Filter API does not pass info to decode(); the decoded block header
    // has already installed exactly the same archived value in Shared::State.
    if (type == BlockType::RECORD) {
      return decodeRecord(output, mode, size, diffFound,
                          structured::unpackRecordInfo(packedInfo));
    }
    if (type == BlockType::NUMERIC) {
      const structured::NumericInfo numeric =
          structured::unpackNumericInfo(packedInfo);
      if (numeric.transform == structured::NumericTransform::BYTE_SHUFFLE) {
        return decodeShuffle(output, mode, size, diffFound,
                             numeric.elementBytes);
      }
      return decodeNumericPredictor(output, mode, size, diffFound, numeric);
    }
    return decodeShuffle(output, mode, size, diffFound,
                         structured::unpackWideTextElementBytes(packedInfo));
  }
};
