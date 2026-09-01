#pragma once

#include "ProfileTypes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace routed {

constexpr int32_t kFeatureScale = 4096;
constexpr uint16_t kMinimumInferredRecordStride = 16;
constexpr uint16_t kMaximumInferredRecordStride = 512;
constexpr size_t kMaximumStrideProbes = 4096;

inline uint8_t fixedByteClass(uint8_t value) {
  if (value == 0)
    return 0;
  if (value == 0xff)
    return 1;
  if (value == ' ' || value == '\t' || value == '\r' || value == '\n')
    return 2;
  if ((value >= '0' && value <= '9') ||
      (value >= 'A' && value <= 'Z') ||
      (value >= 'a' && value <= 'z'))
    return 3;
  if (value >= 0x20 && value <= 0x7e)
    return 4;
  return 5;
}

inline int32_t fixedRatio(uint64_t numerator, uint64_t denominator) {
  if (denominator == 0)
    return 0;
  return static_cast<int32_t>((numerator * kFeatureScale) / denominator);
}

inline bool ratioAtLeast(uint64_t numerator, uint64_t denominator,
                         uint32_t requiredNumerator,
                         uint32_t requiredDenominator) {
  return denominator != 0 &&
         numerator * requiredDenominator >=
           denominator * requiredNumerator;
}

inline bool ratioAtMost(uint64_t numerator, uint64_t denominator,
                        uint32_t allowedNumerator,
                        uint32_t allowedDenominator) {
  return denominator != 0 &&
         numerator * allowedDenominator <= denominator * allowedNumerator;
}

struct FeatureVector {
  uint32_t length = 0;
  std::array<uint32_t, 256> byteCounts{};
  std::array<uint32_t, 6> classCounts{};
  uint32_t printable = 0;
  uint32_t alphanumeric = 0;
  uint32_t zero = 0;
  uint32_t invalidControl = 0;
};

inline FeatureVector makeFeatureVector(const std::vector<uint8_t>& bytes) {
  FeatureVector result;
  result.length = static_cast<uint32_t>(bytes.size());
  for (uint8_t value : bytes) {
    ++result.byteCounts[value];
    ++result.classCounts[fixedByteClass(value)];
    result.zero += static_cast<uint32_t>(value == 0);
    const bool whitespace = value == ' ' || value == '\t' ||
                            value == '\r' || value == '\n';
    const bool visible = value >= 0x20 && value <= 0x7e;
    const bool alphanumeric = (value >= '0' && value <= '9') ||
                              (value >= 'A' && value <= 'Z') ||
                              (value >= 'a' && value <= 'z');
    result.printable += static_cast<uint32_t>(visible || whitespace);
    result.alphanumeric += static_cast<uint32_t>(alphanumeric);
    result.invalidControl +=
      static_cast<uint32_t>(value != 0 && value < 0x20 && !whitespace);
  }
  return result;
}

inline int32_t expectedSameQ12(const FeatureVector& features) {
  uint64_t numerator = 0;
  for (uint32_t count : features.byteCounts)
    numerator += static_cast<uint64_t>(count) * count;
  const uint64_t denominator =
    static_cast<uint64_t>(features.length) * features.length;
  return fixedRatio(numerator, denominator);
}

inline int32_t expectedClassQ12(const FeatureVector& features) {
  uint64_t numerator = 0;
  for (uint32_t count : features.classCounts)
    numerator += static_cast<uint64_t>(count) * count;
  const uint64_t denominator =
    static_cast<uint64_t>(features.length) * features.length;
  return fixedRatio(numerator, denominator);
}

inline int32_t expectedSmallDifferenceQ12(const FeatureVector& features) {
  uint64_t numerator = 0;
  for (size_t value = 0; value < features.byteCounts.size(); ++value) {
    const size_t low = value > 3 ? value - 3 : 0;
    const size_t high = value + 3 < features.byteCounts.size()
      ? value + 3 : features.byteCounts.size() - 1;
    uint64_t nearby = 0;
    for (size_t other = low; other <= high; ++other)
      nearby += features.byteCounts[other];
    numerator += static_cast<uint64_t>(features.byteCounts[value]) * nearby;
  }
  const uint64_t denominator =
    static_cast<uint64_t>(features.length) * features.length;
  return fixedRatio(numerator, denominator);
}

struct StrideFeature {
  uint16_t stride = 0;
  int32_t sameLiftQ12 = 0;
  int32_t classLiftQ12 = 0;
  int32_t smallDifferenceLiftQ12 = 0;
  int32_t scoreQ8 = 0;
  bool valid = false;
};

inline StrideFeature measureStride(const std::vector<uint8_t>& bytes,
                                   const FeatureVector& features,
                                   uint16_t stride) {
  StrideFeature result;
  result.stride = stride;
  if (stride < kMinimumInferredRecordStride || stride >= bytes.size() ||
      bytes.size() / stride < 64)
    return result;

  uint64_t same = 0;
  uint64_t sameClass = 0;
  uint64_t smallDifference = 0;
  const size_t available = bytes.size() - stride;
  const size_t comparisons = std::min(kMaximumStrideProbes, available);
  for (size_t probe = 0; probe < comparisons; ++probe) {
    const size_t relative = comparisons == 1
      ? 0 : ((available - 1) * probe) / (comparisons - 1);
    const size_t index = stride + relative;
    const uint8_t current = bytes[index];
    const uint8_t previous = bytes[index - stride];
    same += static_cast<uint64_t>(current == previous);
    sameClass += static_cast<uint64_t>(
      fixedByteClass(current) == fixedByteClass(previous));
    const unsigned difference = current > previous
      ? static_cast<unsigned>(current - previous)
      : static_cast<unsigned>(previous - current);
    smallDifference += static_cast<uint64_t>(difference <= 3);
  }

  result.sameLiftQ12 = fixedRatio(same, comparisons) -
                       expectedSameQ12(features);
  result.classLiftQ12 = fixedRatio(sameClass, comparisons) -
                        expectedClassQ12(features);
  result.smallDifferenceLiftQ12 = fixedRatio(smallDifference, comparisons) -
                                  expectedSmallDifferenceQ12(features);
  const int32_t combined = 6 * result.sameLiftQ12 +
                           2 * result.classLiftQ12 +
                           result.smallDifferenceLiftQ12 - stride / 2;
  result.scoreQ8 = combined / 16;
  result.valid =
    (result.sameLiftQ12 >= 384 ||
     (result.sameLiftQ12 >= 128 && result.classLiftQ12 >= 512 &&
      result.smallDifferenceLiftQ12 >= 640)) &&
    result.scoreQ8 >= 160;
  return result;
}

inline StrideFeature inferRecordStride(const std::vector<uint8_t>& bytes) {
  StrideFeature best;
  if (bytes.size() < static_cast<size_t>(kMinimumInferredRecordStride) * 64)
    return best;
  const FeatureVector features = makeFeatureVector(bytes);
  const uint16_t maximumStride = static_cast<uint16_t>(
    std::min<size_t>(kMaximumInferredRecordStride, bytes.size() / 64));
  for (uint16_t stride = kMinimumInferredRecordStride;
       stride <= maximumStride; ++stride) {
    const StrideFeature candidate = measureStride(bytes, features, stride);
    if (!candidate.valid)
      continue;
    if (!best.valid || candidate.scoreQ8 > best.scoreQ8 + 16 ||
        (candidate.scoreQ8 + 16 >= best.scoreQ8 &&
         candidate.stride < best.stride))
      best = candidate;
  }
  return best;
}

struct WideTextFeature {
  uint8_t codeUnitBytes = 0;
  uint8_t contentLane = 0;
  int32_t scoreQ8 = 0;
  bool valid = false;
};

struct WideLaneFeature {
  uint32_t count = 0;
  uint32_t zero = 0;
  uint32_t printable = 0;
  uint32_t alphanumeric = 0;
  uint32_t invalidControl = 0;
  uint32_t distinctAscii = 0;
};

inline bool validWideCodeUnits(const std::vector<uint8_t>& bytes,
                               uint8_t width, uint8_t contentLane) {
  if (bytes.size() % width != 0)
    return false;
  if (width == 2) {
    const bool littleEndian = contentLane == 0;
    for (size_t offset = 0; offset < bytes.size(); offset += 2) {
      const uint16_t unit = littleEndian
        ? static_cast<uint16_t>(bytes[offset]) |
            static_cast<uint16_t>(bytes[offset + 1]) << 8
        : static_cast<uint16_t>(bytes[offset]) << 8 |
            static_cast<uint16_t>(bytes[offset + 1]);
      if (unit >= 0xd800 && unit <= 0xdbff) {
        if (offset + 3 >= bytes.size())
          return false;
        const uint16_t next = littleEndian
          ? static_cast<uint16_t>(bytes[offset + 2]) |
              static_cast<uint16_t>(bytes[offset + 3]) << 8
          : static_cast<uint16_t>(bytes[offset + 2]) << 8 |
              static_cast<uint16_t>(bytes[offset + 3]);
        if (next < 0xdc00 || next > 0xdfff)
          return false;
        offset += 2;
      }
      else if (unit >= 0xdc00 && unit <= 0xdfff) {
        return false;
      }
    }
    return true;
  }
  if (width != 4 || (contentLane != 0 && contentLane != 3))
    return false;
  const bool littleEndian = contentLane == 0;
  for (size_t offset = 0; offset < bytes.size(); offset += 4) {
    uint32_t unit = 0;
    if (littleEndian) {
      for (unsigned byte = 0; byte < 4; ++byte)
        unit |= static_cast<uint32_t>(bytes[offset + byte]) << (8 * byte);
    }
    else {
      for (unsigned byte = 0; byte < 4; ++byte)
        unit = (unit << 8) | bytes[offset + byte];
    }
    if (unit > 0x10ffff || (unit >= 0xd800 && unit <= 0xdfff))
      return false;
  }
  return true;
}

inline WideLaneFeature measureWideLane(const std::vector<uint8_t>& bytes,
                                       uint8_t width, uint8_t lane) {
  WideLaneFeature result;
  std::array<bool, 128> seen{};
  for (size_t index = lane; index < bytes.size(); index += width) {
    const uint8_t value = bytes[index];
    ++result.count;
    result.zero += static_cast<uint32_t>(value == 0);
    const bool whitespace = value == ' ' || value == '\t' ||
                            value == '\r' || value == '\n';
    const bool visible = value >= 0x20 && value <= 0x7e;
    const bool alphanumeric = (value >= '0' && value <= '9') ||
                              (value >= 'A' && value <= 'Z') ||
                              (value >= 'a' && value <= 'z');
    result.printable += static_cast<uint32_t>(visible || whitespace);
    result.alphanumeric += static_cast<uint32_t>(alphanumeric);
    result.invalidControl +=
      static_cast<uint32_t>(value != 0 && value < 0x20 && !whitespace);
    if (value < seen.size())
      seen[value] = true;
  }
  for (bool present : seen)
    result.distinctAscii += static_cast<uint32_t>(present);
  return result;
}

inline WideTextFeature inferWideText(const std::vector<uint8_t>& bytes,
                                     uint8_t width) {
  WideTextFeature result;
  result.codeUnitBytes = width;
  if ((width != 2 && width != 4) || bytes.size() < 4096)
    return result;

  std::array<WideLaneFeature, 4> lanes{};
  int64_t bestContentScore = std::numeric_limits<int64_t>::min();
  for (uint8_t lane = 0; lane < width; ++lane) {
    lanes[lane] = measureWideLane(bytes, width, lane);
    const WideLaneFeature& value = lanes[lane];
    const int64_t score = 4 * static_cast<int64_t>(value.printable) +
                          2 * static_cast<int64_t>(value.alphanumeric) -
                          8 * static_cast<int64_t>(value.invalidControl) -
                          static_cast<int64_t>(value.zero) +
                          static_cast<int64_t>(value.distinctAscii) * 8;
    if (score > bestContentScore) {
      bestContentScore = score;
      result.contentLane = lane;
    }
  }

  const WideLaneFeature& content = lanes[result.contentLane];
  uint64_t otherZero = 0;
  uint64_t otherCount = 0;
  bool minimumOtherZero = true;
  for (uint8_t lane = 0; lane < width; ++lane) {
    if (lane == result.contentLane)
      continue;
    otherZero += lanes[lane].zero;
    otherCount += lanes[lane].count;
    minimumOtherZero = minimumOtherZero &&
      ratioAtLeast(lanes[lane].zero, lanes[lane].count,
                   width == 2 ? 74 : 78, 100);
  }
  if (width == 2) {
    result.valid = ratioAtLeast(content.printable, content.count, 83, 100) &&
      ratioAtLeast(content.alphanumeric, content.count, 36, 100) &&
      ratioAtMost(content.invalidControl, content.count, 25, 1000) &&
      ratioAtMost(content.zero, content.count, 15, 100) &&
      content.distinctAscii >= 9 && minimumOtherZero;
  }
  else {
    result.valid = (result.contentLane == 0 || result.contentLane == 3) &&
      ratioAtLeast(content.printable, content.count, 79, 100) &&
      ratioAtLeast(content.alphanumeric, content.count, 33, 100) &&
      ratioAtMost(content.invalidControl, content.count, 20, 1000) &&
      ratioAtMost(content.zero, content.count, 18, 100) &&
      content.distinctAscii >= 8 && minimumOtherZero &&
      ratioAtLeast(otherZero, otherCount, 90, 100);
  }
  if (result.valid) {
    result.valid = validWideCodeUnits(bytes, width, result.contentLane);
  }
  if (result.valid) {
    result.scoreQ8 = 256 +
      fixedRatio(content.printable, content.count) / 16 +
      fixedRatio(otherZero, otherCount) / 16;
  }
  return result;
}

inline WideTextFeature inferWideText(const std::vector<uint8_t>& bytes) {
  const WideTextFeature width2 = inferWideText(bytes, 2);
  const WideTextFeature width4 = inferWideText(bytes, 4);
  if (!width2.valid)
    return width4;
  if (!width4.valid)
    return width2;
  if (width4.scoreQ8 > width2.scoreQ8)
    return width4;
  return width2;
}

} // namespace routed
