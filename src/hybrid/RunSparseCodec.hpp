#pragma once

#include "../Utils.hpp"
#include "ProfileParameters.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace routed {

// Canonical sparse representation: decoded length, nonzero count, followed by
// strictly positive gaps from the previous nonzero byte and the byte value.
// It is implemented but not eligible for automatic routing before calibration.
inline std::vector<uint8_t> encodeRunSparse(const uint8_t* data, size_t length) {
  if (length != 0 && data == nullptr)
    quit("RUN_SPARSE input is unavailable.");
  uint64_t nonzeroCount = 0;
  for (size_t index = 0; index < length; ++index)
    nonzeroCount += static_cast<uint64_t>(data[index] != 0);

  CanonicalWriter writer;
  writer.putUleb128(length);
  writer.putUleb128(nonzeroCount);
  uint64_t previousPlusOne = 0;
  for (size_t index = 0; index < length; ++index) {
    if (data[index] == 0)
      continue;
    const uint64_t currentPlusOne = static_cast<uint64_t>(index) + 1;
    writer.putUleb128(currentPlusOne - previousPlusOne);
    writer.putByte(data[index]);
    previousPlusOne = currentPlusOne;
  }
  return writer.take();
}

inline bool decodeRunSparse(const std::vector<uint8_t>& encoded,
                            uint64_t maximumDecodedLength,
                            std::vector<uint8_t>& decoded) {
  CanonicalReader reader(encoded);
  uint64_t decodedLength = 0;
  uint64_t nonzeroCount = 0;
  if (!reader.readUleb128(decodedLength) ||
      decodedLength > maximumDecodedLength ||
      decodedLength > std::numeric_limits<size_t>::max() ||
      !reader.readUleb128(nonzeroCount) || nonzeroCount > decodedLength)
    return false;
  decoded.assign(static_cast<size_t>(decodedLength), 0);
  uint64_t previousPlusOne = 0;
  for (uint64_t item = 0; item < nonzeroCount; ++item) {
    uint64_t gap = 0;
    uint8_t value = 0;
    if (!reader.readUleb128(gap) || gap == 0 ||
        gap > decodedLength - previousPlusOne ||
        !reader.readByte(value) || value == 0)
      return false;
    const uint64_t currentPlusOne = previousPlusOne + gap;
    decoded[static_cast<size_t>(currentPlusOne - 1)] = value;
    previousPlusOne = currentPlusOne;
  }
  return reader.atEnd();
}

} // namespace routed
