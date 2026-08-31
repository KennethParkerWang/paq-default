#pragma once

#include "../BlockType.hpp"
#include "../file/File.hpp"
#include "StructuredDataFilter.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <vector>

// Encoder-only result.  The decoder consumes only type/info and never repeats
// this classification.
struct SecondaryDecision {
  BlockType type = BlockType::DEFAULT;
  uint32_t info = 0;
  double estimatedGainBpb = 0.0;
};

namespace default_structure_detail {

constexpr uint64_t kMinimumBlockSize = 16u * 1024u;
constexpr size_t kMaximumWindowSize = 64u * 1024u;
constexpr size_t kMinimumRows = 48;
constexpr size_t kMaximumRowBytes = 4096;
constexpr size_t kLagProbeCount = 1536;

class FilePositionRestorer {
public:
  explicit FilePositionRestorer(File* file)
    : file_(file), position_(file != nullptr ? file->curPos() : 0) {}

  ~FilePositionRestorer() {
    if (file_ != nullptr)
      file_->setpos(position_);
  }

  FilePositionRestorer(const FilePositionRestorer&) = delete;
  FilePositionRestorer& operator=(const FilePositionRestorer&) = delete;

private:
  File* file_;
  uint64_t position_;
};

struct ProxyCosts {
  double order0 = 8.0;
  double order1 = 8.0;
  double mixed = 8.0;
};

struct RankedParameter {
  uint16_t value = 0;
  double score = -std::numeric_limits<double>::infinity();
};

struct LagStatistics {
  double expectedSame = 0.0;
  double expectedClass = 0.0;
  double expectedSmallDifference = 0.0;
};

struct Candidate {
  BlockType type = BlockType::DEFAULT;
  uint32_t info = 0;
  double gain = 0.0;
  double score = -std::numeric_limits<double>::infinity();
  bool valid = false;
};

struct LaneProfile {
  size_t count = 0;
  size_t zero = 0;
  size_t printable = 0;
  size_t alphanumeric = 0;
  size_t invalidControl = 0;
  size_t distinct = 0;
};

struct WideEvidence {
  bool valid = false;
  uint8_t width = 0;
  uint8_t contentLane = 0;
  double confidence = 0.0;
};

inline double ratio(const size_t numerator, const size_t denominator) {
  return denominator == 0 ? 0.0
                          : static_cast<double>(numerator) / static_cast<double>(denominator);
}

inline uint8_t byteClass(const uint8_t c) {
  if (c == 0)
    return 0;
  if (c == 0xff)
    return 1;
  if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
    return 2;
  if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
    return 3;
  if (c >= 0x20 && c <= 0x7e)
    return 4;
  return 5;
}

inline bool readWindow(File* const in, const uint64_t position, const size_t size,
                       std::vector<uint8_t>& destination) {
  destination.resize(size);
  in->setpos(position);
  return in->blockRead(destination.data(), size) == size;
}

// A smoothed adaptive proxy is deliberately used instead of plug-in H1.
// A 64 KiB random sample leaves many of the 65,536 byte pairs sparse, so raw
// empirical conditional entropy would otherwise reward arbitrary reorderings.
inline ProxyCosts proxyCosts(const std::vector<uint8_t>& data) {
  ProxyCosts result;
  if (data.size() < 2)
    return result;

  std::array<uint32_t, 256> order0Counts{};
  order0Counts.fill(1);
  uint64_t order0Total = 256;
  double order0Bits = 0.0;
  for (const uint8_t c : data) {
    order0Bits += std::log2(static_cast<double>(order0Total) /
                            static_cast<double>(order0Counts[c]));
    ++order0Counts[c];
    ++order0Total;
  }

  std::array<uint32_t, 256> globalCounts{};
  std::array<uint32_t, 256> contextTotals{};
  std::vector<uint32_t> pairCounts(65536, 0);
  globalCounts.fill(1);
  uint64_t globalTotal = 256;
  constexpr double priorWeight = 28.0;
  double order1Bits = 0.0;

  uint8_t previous = data.front();
  ++globalCounts[previous];
  ++globalTotal;
  for (size_t i = 1; i < data.size(); ++i) {
    const uint8_t current = data[i];
    const size_t pair = (static_cast<size_t>(previous) << 8) | current;
    const double globalProbability = static_cast<double>(globalCounts[current]) /
                                     static_cast<double>(globalTotal);
    const double probability =
      (static_cast<double>(pairCounts[pair]) + priorWeight * globalProbability) /
      (static_cast<double>(contextTotals[previous]) + priorWeight);
    order1Bits -= std::log2(probability);

    ++pairCounts[pair];
    ++contextTotals[previous];
    ++globalCounts[current];
    ++globalTotal;
    previous = current;
  }

  result.order0 = order0Bits / static_cast<double>(data.size());
  result.order1 = order1Bits / static_cast<double>(data.size() - 1);
  // Generic PAQ can use both symbol-frequency and byte-context evidence.  The
  // weighting is a ranking proxy, not an archive-format parameter.
  result.mixed = 0.27 * result.order0 + 0.73 * result.order1;
  return result;
}

inline void keepBest(std::vector<RankedParameter>& ranked,
                     const RankedParameter candidate, const size_t limit) {
  ranked.push_back(candidate);
  std::sort(ranked.begin(), ranked.end(), [](const RankedParameter& a,
                                              const RankedParameter& b) {
    if (a.score != b.score)
      return a.score > b.score;
    return a.value < b.value;
  });
  if (ranked.size() > limit)
    ranked.resize(limit);
}

inline double expectedSmallDifference(const std::array<size_t, 256>& histogram,
                                      const size_t n) {
  if (n == 0)
    return 0.0;
  long double pairs = 0.0;
  for (size_t a = 0; a < 256; ++a) {
    const size_t low = a > 3 ? a - 3 : 0;
    const size_t high = std::min<size_t>(255, a + 3);
    size_t nearby = 0;
    for (size_t b = low; b <= high; ++b)
      nearby += histogram[b];
    pairs += static_cast<long double>(histogram[a]) * nearby;
  }
  const long double denominator = static_cast<long double>(n) * n;
  return static_cast<double>(pairs / denominator);
}

inline LagStatistics makeLagStatistics(const std::vector<uint8_t>& data) {
  LagStatistics result;
  if (data.empty())
    return result;

  std::array<size_t, 256> byteHistogram{};
  std::array<size_t, 6> classHistogram{};
  for (const uint8_t c : data) {
    ++byteHistogram[c];
    ++classHistogram[byteClass(c)];
  }

  const long double n2 = static_cast<long double>(data.size()) * data.size();
  long double sameExpectedNumerator = 0.0;
  long double classExpectedNumerator = 0.0;
  for (const size_t count : byteHistogram)
    sameExpectedNumerator += static_cast<long double>(count) * count;
  for (const size_t count : classHistogram)
    classExpectedNumerator += static_cast<long double>(count) * count;

  result.expectedSame = static_cast<double>(sameExpectedNumerator / n2);
  result.expectedClass = static_cast<double>(classExpectedNumerator / n2);
  result.expectedSmallDifference = expectedSmallDifference(byteHistogram, data.size());
  return result;
}

// Cheap, bounded lag response used only to shortlist candidates.  Full
// transform/model proxies below make the actual decision.
inline double lagResponse(const std::vector<uint8_t>& data, const size_t lag,
                          const LagStatistics& statistics) {
  if (lag == 0 || lag >= data.size())
    return -std::numeric_limits<double>::infinity();

  const size_t available = data.size() - lag;
  const size_t probes = std::min(kLagProbeCount, available);
  size_t same = 0;
  size_t sameClass = 0;
  size_t smallDifference = 0;
  for (size_t p = 0; p < probes; ++p) {
    const size_t offset = probes == 1 ? 0 : ((available - 1) * p) / (probes - 1);
    const size_t i = lag + offset;
    const uint8_t a = data[i];
    const uint8_t b = data[i - lag];
    same += static_cast<size_t>(a == b);
    sameClass += static_cast<size_t>(byteClass(a) == byteClass(b));
    const unsigned difference = a > b ? static_cast<unsigned>(a - b)
                                      : static_cast<unsigned>(b - a);
    smallDifference += static_cast<size_t>(difference <= 3);
  }

  const double observedSame = ratio(same, probes);
  const double observedClass = ratio(sameClass, probes);
  const double observedSmall = ratio(smallDifference, probes);
  return 1.45 * (observedSame - statistics.expectedSame) +
         0.40 * (observedClass - statistics.expectedClass) +
         0.30 * (observedSmall - statistics.expectedSmallDifference);
}

inline double recordModelCost(const std::vector<uint8_t>& data, const size_t stride) {
  if (stride < 2 || stride >= data.size())
    return 8.0;

  std::vector<uint32_t> positionCounts(stride * 256, 0);
  std::vector<uint32_t> positionTotals(stride, 0);
  std::vector<uint32_t> lagPairCounts(65536, 0);
  std::array<uint32_t, 256> lagTotals{};
  std::array<uint32_t, 256> globalCounts{};
  globalCounts.fill(1);
  uint64_t globalTotal = 256;
  constexpr double positionPrior = 24.0;
  constexpr double lagPrior = 32.0;
  double bits = 0.0;

  for (size_t i = 0; i < data.size(); ++i) {
    const uint8_t current = data[i];
    const size_t column = i % stride;
    const double globalProbability = static_cast<double>(globalCounts[current]) /
                                     static_cast<double>(globalTotal);
    const size_t positionIndex = column * 256 + current;
    const double positionProbability =
      (static_cast<double>(positionCounts[positionIndex]) +
       positionPrior * globalProbability) /
      (static_cast<double>(positionTotals[column]) + positionPrior);

    double probability = positionProbability;
    if (i >= stride) {
      const uint8_t previousRecord = data[i - stride];
      const size_t pairIndex = (static_cast<size_t>(previousRecord) << 8) | current;
      const double lagProbability =
        (static_cast<double>(lagPairCounts[pairIndex]) + lagPrior * globalProbability) /
        (static_cast<double>(lagTotals[previousRecord]) + lagPrior);
      probability = 0.57 * positionProbability + 0.43 * lagProbability;
      ++lagPairCounts[pairIndex];
      ++lagTotals[previousRecord];
    }

    bits -= std::log2(probability);
    ++positionCounts[positionIndex];
    ++positionTotals[column];
    ++globalCounts[current];
    ++globalTotal;
  }
  return bits / static_cast<double>(data.size());
}

inline std::vector<uint8_t> recordTranspose(const std::vector<uint8_t>& source,
                                            const size_t stride, const bool delta) {
  std::vector<uint8_t> transformed(source.size());
  if (delta) {
    structured::recordTransposeDeltaForward(source.data(), transformed.data(),
                                            source.size(), stride);
  }
  else {
    structured::recordTransposeForward(source.data(), transformed.data(),
                                       source.size(), stride);
  }
  return transformed;
}

inline double stableScore(const double firstGain, const double secondGain,
                          const double complexityPenalty) {
  return std::min(firstGain, secondGain) -
         0.22 * std::abs(firstGain - secondGain) - complexityPenalty;
}

inline void considerCandidate(Candidate& best, const BlockType type, const uint32_t info,
                              const double firstGain, const double secondGain,
                              const double minimumWindowGain, const double penalty,
                              const double evidenceBonus = 0.0) {
  if (!std::isfinite(firstGain) || !std::isfinite(secondGain) ||
      firstGain < minimumWindowGain || secondGain < minimumWindowGain)
    return;
  const double score = stableScore(firstGain, secondGain, penalty) + evidenceBonus;
  if (!best.valid || score > best.score) {
    best.type = type;
    best.info = info;
    best.gain = std::min(firstGain, secondGain);
    best.score = score;
    best.valid = true;
  }
}

inline Candidate detectRecord(const std::vector<uint8_t>& first,
                              const std::vector<uint8_t>& second,
                              const ProxyCosts& firstBaseline,
                              const ProxyCosts& secondBaseline) {
  Candidate best;
  const size_t maximumStride = std::min<size_t>(512, first.size() / 64);
  if (maximumStride < 2)
    return best;

  const LagStatistics firstStatistics = makeLagStatistics(first);
  const LagStatistics secondStatistics = makeLagStatistics(second);
  std::vector<RankedParameter> shortlist;
  for (size_t stride = 2; stride <= maximumStride; ++stride) {
    // The adaptive full proxy also charges cold-start cost.  This small rank
    // penalty prevents harmonics with hundreds of fields from crowding out a
    // simpler fundamental period.
    const double rankPenalty = 0.0035 * std::log2(static_cast<double>(stride)) +
                               0.55 * static_cast<double>(stride) / first.size();
    keepBest(shortlist,
             {static_cast<uint16_t>(stride),
              lagResponse(first, stride, firstStatistics) - rankPenalty},
             12);
  }

  for (const RankedParameter ranked : shortlist) {
    const size_t stride = ranked.value;
    const double validationResponse = lagResponse(second, stride, secondStatistics);
    if (ranked.score <= 0.0 || validationResponse <= 0.0)
      continue;

    const double sizePenalty = 0.006 * std::log2(static_cast<double>(stride)) +
                               0.85 * static_cast<double>(stride) / first.size();
    const double evidence = std::min(0.035, 0.035 * std::min(ranked.score,
                                                             validationResponse));

    const double firstModelGain =
      0.60 * std::max(0.0, firstBaseline.mixed - recordModelCost(first, stride));
    const double secondModelGain =
      0.60 * std::max(0.0, secondBaseline.mixed - recordModelCost(second, stride));
    considerCandidate(best, BlockType::RECORD,
                      structured::packRecordInfo(static_cast<uint16_t>(stride),
                                                 structured::RecordTransform::MODEL_ONLY),
                      firstModelGain, secondModelGain, 0.075, sizePenalty, evidence);

    const std::vector<uint8_t> firstTransposed = recordTranspose(first, stride, false);
    const std::vector<uint8_t> secondTransposed = recordTranspose(second, stride, false);
    const double firstTransposeGain = firstBaseline.mixed - proxyCosts(firstTransposed).mixed;
    const double secondTransposeGain = secondBaseline.mixed - proxyCosts(secondTransposed).mixed;
    considerCandidate(best, BlockType::RECORD,
                      structured::packRecordInfo(static_cast<uint16_t>(stride),
                                                 structured::RecordTransform::TRANSPOSE),
                      firstTransposeGain, secondTransposeGain, 0.105,
                      sizePenalty + 0.018, evidence);

    const std::vector<uint8_t> firstDelta = recordTranspose(first, stride, true);
    const std::vector<uint8_t> secondDelta = recordTranspose(second, stride, true);
    const double firstDeltaGain = firstBaseline.mixed - proxyCosts(firstDelta).mixed;
    const double secondDeltaGain = secondBaseline.mixed - proxyCosts(secondDelta).mixed;
    considerCandidate(best, BlockType::RECORD,
                      structured::packRecordInfo(static_cast<uint16_t>(stride),
                                                 structured::RecordTransform::TRANSPOSE_DELTA),
                      firstDeltaGain, secondDeltaGain, 0.125,
                      sizePenalty + 0.040, evidence);
  }
  return best;
}

inline std::vector<uint8_t> byteShuffle(const std::vector<uint8_t>& source,
                                        const uint8_t elementBytes) {
  std::vector<uint8_t> transformed(source.size());
  structured::byteShuffleForward(source.data(), transformed.data(), source.size(),
                                 elementBytes);
  return transformed;
}

inline std::vector<uint8_t> numericResidual(const std::vector<uint8_t>& source,
                                            const uint8_t elementBytes,
                                            const uint16_t rowWidthElements,
                                            const bool bigEndian, const bool lorenzo,
                                            const bool shuffle) {
  // detectNumericPredictor() is the sole caller and enumerates only 1- and
  // 2-byte elements; 4/8-byte candidates are restricted to BYTE_SHUFFLE.
  std::vector<uint8_t> residual(source.size());
  if (elementBytes == 1) {
    if (lorenzo) {
      structured::lorenzo8Forward(source.data(), residual.data(), source.size(),
                                  rowWidthElements);
    }
    else {
      structured::vertical8Forward(source.data(), residual.data(), source.size(),
                                   rowWidthElements);
    }
  }
  else {
    if (lorenzo) {
      structured::lorenzo16Forward(source.data(), residual.data(), source.size(),
                                   rowWidthElements, bigEndian);
    }
    else {
      structured::vertical16Forward(source.data(), residual.data(), source.size(),
                                    rowWidthElements, bigEndian);
    }
  }
  if (!shuffle)
    return residual;

  const size_t rowBytes = static_cast<size_t>(rowWidthElements) * elementBytes;
  const size_t predictedBytes = (source.size() / rowBytes) * rowBytes;
  std::vector<uint8_t> transformed(source.size());
  structured::byteShuffleForward(residual.data(), transformed.data(), predictedBytes,
                                 elementBytes);
  std::copy(residual.begin() + predictedBytes, residual.end(),
            transformed.begin() + predictedBytes);
  return transformed;
}

inline double laneHeterogeneity(const std::vector<uint8_t>& data,
                                const uint8_t elementBytes) {
  if (elementBytes < 2)
    return 0.0;
  const size_t elements = data.size() / elementBytes;
  if (elements == 0)
    return 0.0;

  std::array<size_t, 256> totalHistogram{};
  std::vector<std::array<size_t, 256>> laneHistograms(elementBytes);
  for (auto& histogram : laneHistograms)
    histogram.fill(0);
  for (size_t element = 0; element < elements; ++element) {
    for (uint8_t lane = 0; lane < elementBytes; ++lane) {
      const uint8_t c = data[element * elementBytes + lane];
      ++totalHistogram[c];
      ++laneHistograms[lane][c];
    }
  }

  const auto entropy = [](const std::array<size_t, 256>& histogram,
                          const size_t count) {
    double value = 0.0;
    for (const size_t frequency : histogram) {
      if (frequency != 0) {
        const double probability = static_cast<double>(frequency) / count;
        value -= probability * std::log2(probability);
      }
    }
    return value;
  };

  const double totalEntropy = entropy(totalHistogram, elements * elementBytes);
  double laneEntropy = 0.0;
  for (const auto& histogram : laneHistograms)
    laneEntropy += entropy(histogram, elements);
  laneEntropy /= elementBytes;

  // Separate lane histograms have more degrees of freedom.  A small explicit
  // correction makes short/high-width samples less eager to claim structure.
  const double estimationPenalty =
    0.5 * static_cast<double>(elementBytes - 1) * 255.0 /
    (static_cast<double>(elements * elementBytes) * std::log(2.0));
  return totalEntropy - laneEntropy - estimationPenalty;
}

inline Candidate detectNumericShuffle(const std::vector<uint8_t>& first,
                                      const std::vector<uint8_t>& second,
                                      const ProxyCosts& firstBaseline,
                                      const ProxyCosts& secondBaseline) {
  Candidate best;
  for (const uint8_t elementBytes : {uint8_t{2}, uint8_t{4}, uint8_t{8}}) {
    const double firstLaneEvidence = laneHeterogeneity(first, elementBytes);
    const double secondLaneEvidence = laneHeterogeneity(second, elementBytes);
    if (firstLaneEvidence < 0.10 || secondLaneEvidence < 0.10)
      continue;
    const double firstGain = firstBaseline.mixed - proxyCosts(byteShuffle(first, elementBytes)).mixed;
    const double secondGain = secondBaseline.mixed - proxyCosts(byteShuffle(second, elementBytes)).mixed;
    const double evidence = std::min(0.025, 0.035 * std::min(firstLaneEvidence,
                                                             secondLaneEvidence));
    considerCandidate(best, BlockType::NUMERIC,
                      structured::packNumericInfo(0, elementBytes, false,
                                                  structured::NumericTransform::BYTE_SHUFFLE),
                      firstGain, secondGain, 0.115, 0.025, evidence);
  }
  return best;
}

inline Candidate detectNumericPredictor(const std::vector<uint8_t>& first,
                                        const std::vector<uint8_t>& second,
                                        const ProxyCosts& firstBaseline,
                                        const ProxyCosts& secondBaseline) {
  Candidate best;
  const LagStatistics firstStatistics = makeLagStatistics(first);
  const LagStatistics secondStatistics = makeLagStatistics(second);
  for (const uint8_t elementBytes : {uint8_t{1}, uint8_t{2}}) {
    const size_t maximumWidth = std::min<size_t>(
      kMaximumRowBytes / elementBytes,
      first.size() / (kMinimumRows * elementBytes));
    if (maximumWidth < 8)
      continue;

    std::vector<RankedParameter> shortlist;
    for (size_t width = 8; width <= maximumWidth; ++width) {
      const size_t byteLag = width * elementBytes;
      const double rankPenalty = 0.0025 * std::log2(static_cast<double>(width)) +
                                 0.40 / static_cast<double>(first.size() / byteLag);
      keepBest(shortlist,
               {static_cast<uint16_t>(width),
                lagResponse(first, byteLag, firstStatistics) - rankPenalty},
               8);
    }

    for (const RankedParameter ranked : shortlist) {
      const uint16_t width = ranked.value;
      const size_t byteLag = static_cast<size_t>(width) * elementBytes;
      const double validationResponse = lagResponse(second, byteLag, secondStatistics);
      if (ranked.score <= 0.0 || validationResponse <= 0.0)
        continue;

      const double widthPenalty =
        0.0045 * std::log2(static_cast<double>(width)) +
        0.65 / static_cast<double>(first.size() / byteLag);
      const double evidence = std::min(0.030, 0.030 * std::min(ranked.score,
                                                               validationResponse));
      const int endianCount = elementBytes == 1 ? 1 : 2;
      for (int endianIndex = 0; endianIndex < endianCount; ++endianIndex) {
        const bool bigEndian = endianIndex != 0;

        const std::vector<uint8_t> firstVertical =
          numericResidual(first, elementBytes, width, bigEndian, false, false);
        const std::vector<uint8_t> secondVertical =
          numericResidual(second, elementBytes, width, bigEndian, false, false);
        considerCandidate(best, BlockType::NUMERIC,
                          structured::packNumericInfo(width, elementBytes, bigEndian,
                                                      structured::NumericTransform::VERTICAL),
                          firstBaseline.mixed - proxyCosts(firstVertical).mixed,
                          secondBaseline.mixed - proxyCosts(secondVertical).mixed,
                          0.120, widthPenalty + 0.018, evidence);

        const std::vector<uint8_t> firstLorenzo =
          numericResidual(first, elementBytes, width, bigEndian, true, false);
        const std::vector<uint8_t> secondLorenzo =
          numericResidual(second, elementBytes, width, bigEndian, true, false);
        considerCandidate(best, BlockType::NUMERIC,
                          structured::packNumericInfo(width, elementBytes, bigEndian,
                                                      structured::NumericTransform::LORENZO),
                          firstBaseline.mixed - proxyCosts(firstLorenzo).mixed,
                          secondBaseline.mixed - proxyCosts(secondLorenzo).mixed,
                          0.135, widthPenalty + 0.030, evidence);

        if (elementBytes == 2) {
          const std::vector<uint8_t> firstLorenzoShuffle =
            numericResidual(first, elementBytes, width, bigEndian, true, true);
          const std::vector<uint8_t> secondLorenzoShuffle =
            numericResidual(second, elementBytes, width, bigEndian, true, true);
          considerCandidate(best, BlockType::NUMERIC,
                            structured::packNumericInfo(
                              width, elementBytes, bigEndian,
                              structured::NumericTransform::LORENZO_SHUFFLE),
                            firstBaseline.mixed - proxyCosts(firstLorenzoShuffle).mixed,
                            secondBaseline.mixed - proxyCosts(secondLorenzoShuffle).mixed,
                            0.150, widthPenalty + 0.045, evidence);
        }
      }
    }
  }
  return best;
}

inline Candidate detectNumeric(const std::vector<uint8_t>& first,
                               const std::vector<uint8_t>& second,
                               const ProxyCosts& firstBaseline,
                               const ProxyCosts& secondBaseline) {
  Candidate best = detectNumericShuffle(first, second, firstBaseline, secondBaseline);
  const Candidate predictor = detectNumericPredictor(first, second, firstBaseline, secondBaseline);
  if (predictor.valid && (!best.valid || predictor.score > best.score))
    best = predictor;
  return best;
}

inline LaneProfile laneProfile(const std::vector<uint8_t>& data, const uint8_t width,
                               const uint8_t lane) {
  LaneProfile profile;
  std::array<bool, 128> seen{};
  for (size_t i = lane; i < data.size(); i += width) {
    const uint8_t c = data[i];
    ++profile.count;
    profile.zero += static_cast<size_t>(c == 0);
    const bool whitespace = c == ' ' || c == '\t' || c == '\r' || c == '\n';
    const bool visible = c >= 0x20 && c <= 0x7e;
    const bool alphanumeric = (c >= '0' && c <= '9') ||
                              (c >= 'A' && c <= 'Z') ||
                              (c >= 'a' && c <= 'z');
    profile.printable += static_cast<size_t>(visible || whitespace);
    profile.alphanumeric += static_cast<size_t>(alphanumeric);
    profile.invalidControl += static_cast<size_t>(c != 0 && c < 0x20 && !whitespace);
    if (c < seen.size())
      seen[c] = true;
  }
  for (const bool present : seen)
    profile.distinct += static_cast<size_t>(present);
  return profile;
}

inline WideEvidence wideEvidence(const std::vector<uint8_t>& data, const uint8_t width) {
  WideEvidence result;
  result.width = width;
  std::array<LaneProfile, 4> lanes{};
  for (uint8_t lane = 0; lane < width; ++lane)
    lanes[lane] = laneProfile(data, width, lane);

  double bestContentScore = -1.0;
  for (uint8_t lane = 0; lane < width; ++lane) {
    const LaneProfile& profile = lanes[lane];
    const double printable = ratio(profile.printable, profile.count);
    const double alphanumeric = ratio(profile.alphanumeric, profile.count);
    const double invalid = ratio(profile.invalidControl, profile.count);
    const double zero = ratio(profile.zero, profile.count);
    const double diversity = std::min(1.0, static_cast<double>(profile.distinct) / 18.0);
    const double contentScore = 0.52 * printable + 0.28 * alphanumeric +
                                0.20 * diversity - invalid - 0.35 * zero;
    if (contentScore > bestContentScore) {
      bestContentScore = contentScore;
      result.contentLane = lane;
    }
  }

  const LaneProfile& content = lanes[result.contentLane];
  const double printable = ratio(content.printable, content.count);
  const double alphanumeric = ratio(content.alphanumeric, content.count);
  const double invalid = ratio(content.invalidControl, content.count);
  const double contentZero = ratio(content.zero, content.count);
  double minimumOtherZero = 1.0;
  double averageOtherZero = 0.0;
  for (uint8_t lane = 0; lane < width; ++lane) {
    if (lane == result.contentLane)
      continue;
    const double zero = ratio(lanes[lane].zero, lanes[lane].count);
    minimumOtherZero = std::min(minimumOtherZero, zero);
    averageOtherZero += zero;
  }
  averageOtherZero /= width - 1;

  if (width == 2) {
    result.valid = printable >= 0.83 && alphanumeric >= 0.36 && invalid <= 0.025 &&
                   contentZero <= 0.15 && content.distinct >= 9 &&
                   minimumOtherZero >= 0.74;
    result.confidence = std::min(printable - 0.78, minimumOtherZero - 0.68) +
                        0.20 * alphanumeric;
  }
  else {
    result.valid = printable >= 0.79 && alphanumeric >= 0.33 && invalid <= 0.020 &&
                   contentZero <= 0.18 && content.distinct >= 8 &&
                   minimumOtherZero >= 0.78 && averageOtherZero >= 0.90;
    result.confidence = std::min(printable - 0.74, averageOtherZero - 0.84) +
                        0.18 * alphanumeric;
  }
  return result;
}

inline Candidate detectWideText(const std::vector<uint8_t>& first,
                                const std::vector<uint8_t>& second,
                                const ProxyCosts& firstBaseline,
                                const ProxyCosts& secondBaseline) {
  Candidate best;
  for (const uint8_t width : {uint8_t{2}, uint8_t{4}}) {
    const WideEvidence firstEvidence = wideEvidence(first, width);
    const WideEvidence secondEvidence = wideEvidence(second, width);
    if (!firstEvidence.valid || !secondEvidence.valid ||
        firstEvidence.contentLane != secondEvidence.contentLane)
      continue;

    const double firstGain = firstBaseline.mixed - proxyCosts(byteShuffle(first, width)).mixed;
    const double secondGain = secondBaseline.mixed - proxyCosts(byteShuffle(second, width)).mixed;
    // Specific ASCII/NUL-lane evidence breaks an otherwise artificial tie
    // with NUMERIC BYTE_SHUFFLE, whose physical transform is intentionally the
    // same but whose model routing and archive meaning differ.
    const double specificity =
      0.045 + std::min(0.045, 0.12 * std::min(firstEvidence.confidence,
                                              secondEvidence.confidence));
    considerCandidate(best, BlockType::WIDE_TEXT,
                      structured::packWideTextInfo(width), firstGain, secondGain,
                      0.095, 0.010, specificity);
  }
  return best;
}

} // namespace default_structure_detail

// Examine two deterministic, non-overlapping encoder-side windows.  At most
// 128 KiB is read regardless of block size.  Every exit restores File::curPos().
inline SecondaryDecision detectDefaultStructure(File* const in,
                                                const uint64_t blockStart,
                                                const uint64_t blockSize) {
  SecondaryDecision fallback;
  if (in == nullptr)
    return fallback;

  default_structure_detail::FilePositionRestorer restore(in);
  if (blockSize < default_structure_detail::kMinimumBlockSize ||
      blockStart > std::numeric_limits<uint64_t>::max() - blockSize)
    return fallback;

  size_t windowSize = static_cast<size_t>(std::min<uint64_t>(
    default_structure_detail::kMaximumWindowSize, blockSize / 2));
  windowSize &= ~size_t{7};
  if (windowSize < 4096)
    return fallback;

  const uint64_t secondOffset = (blockSize - windowSize) & ~uint64_t{7};
  if (secondOffset < windowSize)
    return fallback;

  std::vector<uint8_t> first;
  std::vector<uint8_t> second;
  if (!default_structure_detail::readWindow(in, blockStart, windowSize, first) ||
      !default_structure_detail::readWindow(in, blockStart + secondOffset, windowSize, second))
    return fallback;

  const default_structure_detail::ProxyCosts firstBaseline =
    default_structure_detail::proxyCosts(first);
  const default_structure_detail::ProxyCosts secondBaseline =
    default_structure_detail::proxyCosts(second);

  // There is intentionally no entropy-first rejection here.  A high-H0 byte
  // stream can still have strong record, row, or lane structure.
  std::array<default_structure_detail::Candidate, 3> families = {
    default_structure_detail::detectRecord(first, second, firstBaseline, secondBaseline),
    default_structure_detail::detectNumeric(first, second, firstBaseline, secondBaseline),
    default_structure_detail::detectWideText(first, second, firstBaseline, secondBaseline)
  };
  std::sort(families.begin(), families.end(),
            [](const default_structure_detail::Candidate& a,
               const default_structure_detail::Candidate& b) {
    return a.score > b.score;
  });

  if (!families[0].valid)
    return fallback;
  constexpr double minimumDecisionScore = 0.060;
  constexpr double minimumWinningMargin = 0.040;
  const double runnerUpScore = families[1].valid ? families[1].score : 0.0;
  if (families[0].score < minimumDecisionScore ||
      families[0].score - runnerUpScore < minimumWinningMargin)
    return fallback;

  SecondaryDecision decision;
  decision.type = families[0].type;
  decision.info = families[0].info;
  decision.estimatedGainBpb = families[0].gain;
  return decision;
}
