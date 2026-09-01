#pragma once

#include "../Utils.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace routed {

// Version 1 is a fixed 64-byte little-endian decoder contract.  Detection
// options are intentionally absent: they influence encoder planning, not the
// already archived PAQ block stream.  Every option below can change predictor
// state, a reversible transform, or arithmetic-decoder probabilities.
constexpr uint16_t kPaqDecoderConfigRevisionV1 = 1;
constexpr size_t kPaqConfigV1CanonicalSize = 64;

enum class PaqPredictorMode : uint8_t {
  CONTEXT_MIXING = 0,
  LSTM_ONLY = 1
};

enum class PaqOptionFlag : uint16_t {
  TRAIN_EXE = 1u << 0,
  TRAIN_TEXT = 1u << 1,
  ADAPTIVE_LEARNING_RATE = 1u << 2,
  SKIP_RGB_TRANSFORM = 1u << 3,
  USE_LSTM = 1u << 4
};

constexpr uint16_t paqOptionFlag(PaqOptionFlag flag) {
  return static_cast<uint16_t>(flag);
}

constexpr bool hasPaqOption(uint16_t flags, PaqOptionFlag flag) {
  return (flags & paqOptionFlag(flag)) != 0;
}

constexpr uint16_t kKnownPaqOptionFlags =
  paqOptionFlag(PaqOptionFlag::TRAIN_EXE) |
  paqOptionFlag(PaqOptionFlag::TRAIN_TEXT) |
  paqOptionFlag(PaqOptionFlag::ADAPTIVE_LEARNING_RATE) |
  paqOptionFlag(PaqOptionFlag::SKIP_RGB_TRANSFORM) |
  paqOptionFlag(PaqOptionFlag::USE_LSTM);

struct PaqConfigV1 {
  uint16_t configRevision = kPaqDecoderConfigRevisionV1;
  uint16_t paqCoreRevision = 216;
  uint16_t arithmeticRevision = 1;
  uint16_t blockStreamRevision = 1;
  uint16_t transformTableRevision = 1;
  uint16_t modelRuleRevision = 1;

  uint8_t compressionLevel = 1;
  PaqPredictorMode predictorMode = PaqPredictorMode::CONTEXT_MIXING;
  uint16_t optionFlags = 0;

  // Stable model-memory class, not a byte count.  Revisioned model code maps
  // this value to concrete allocations; zero means the level-derived default.
  uint32_t modelMemoryClass = 0;

  uint8_t lstmLayers = 0;
  uint8_t lstmHiddenSize = 0;
  uint8_t lstmHorizon = 0;

  // Exact IEEE-754 binary32 bits.  Keeping bits rather than float prevents
  // locale/decimal conversions from changing a decoder-affecting value.
  uint32_t tuningParameterBits = 0;

  // Reserved for a future bundled/frozen LSTM state. Native routed v1 rejects
  // LSTM because the legacy initializer uses process-global std::rand() and
  // therefore is not an archive-self-contained decoder contract.
  std::array<uint8_t, 32> lstmWeightDigest{};

  bool operator==(const PaqConfigV1& other) const {
    return configRevision == other.configRevision &&
           paqCoreRevision == other.paqCoreRevision &&
           arithmeticRevision == other.arithmeticRevision &&
           blockStreamRevision == other.blockStreamRevision &&
           transformTableRevision == other.transformTableRevision &&
           modelRuleRevision == other.modelRuleRevision &&
           compressionLevel == other.compressionLevel &&
           predictorMode == other.predictorMode &&
           optionFlags == other.optionFlags &&
           modelMemoryClass == other.modelMemoryClass &&
           lstmLayers == other.lstmLayers &&
           lstmHiddenSize == other.lstmHiddenSize &&
           lstmHorizon == other.lstmHorizon &&
           tuningParameterBits == other.tuningParameterBits &&
           lstmWeightDigest == other.lstmWeightDigest;
  }

  bool operator!=(const PaqConfigV1& other) const { return !(*this == other); }
};

using PaqConfigId = std::array<uint8_t, 32>;

namespace paq_config_detail {

constexpr std::array<uint8_t, 4> kMagic = {'P', 'Q', 'C', '1'};

inline void append16(std::vector<uint8_t>& output, uint16_t value) {
  output.push_back(static_cast<uint8_t>(value));
  output.push_back(static_cast<uint8_t>(value >> 8));
}

inline void append32(std::vector<uint8_t>& output, uint32_t value) {
  for (unsigned index = 0; index < 4; ++index)
    output.push_back(static_cast<uint8_t>(value >> (8 * index)));
}

inline uint16_t load16(const uint8_t* input) {
  return static_cast<uint16_t>(input[0]) |
         static_cast<uint16_t>(input[1]) << 8;
}

inline uint32_t load32(const uint8_t* input) {
  uint32_t value = 0;
  for (unsigned index = 0; index < 4; ++index)
    value |= static_cast<uint32_t>(input[index]) << (8 * index);
  return value;
}

inline void store64(uint8_t* output, uint64_t value) {
  for (unsigned index = 0; index < 8; ++index)
    output[index] = static_cast<uint8_t>(value >> (8 * index));
}

inline bool allZero(const std::array<uint8_t, 32>& value) {
  for (uint8_t byte : value) {
    if (byte != 0)
      return false;
  }
  return true;
}

inline uint64_t domainFnv1a(const std::vector<uint8_t>& bytes,
                            uint64_t domain) {
  uint64_t hash = UINT64_C(1469598103934665603) ^
                  (domain * UINT64_C(0x9e3779b97f4a7c15));
  const auto mix = [&hash](uint8_t byte) {
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  };
  mix('P');
  mix('A');
  mix('Q');
  mix(static_cast<uint8_t>(domain));
  for (uint8_t byte : bytes)
    mix(byte);
  const uint64_t length = static_cast<uint64_t>(bytes.size());
  for (unsigned index = 0; index < 8; ++index)
    mix(static_cast<uint8_t>(length >> (8 * index)));
  return hash;
}

} // namespace paq_config_detail

inline bool validPaqConfigV1(const PaqConfigV1& value) {
  if (value.configRevision != kPaqDecoderConfigRevisionV1 ||
      value.paqCoreRevision != 216 || value.arithmeticRevision != 1 ||
      value.blockStreamRevision != 1 ||
      value.transformTableRevision != 1 || value.modelRuleRevision != 1 ||
      value.compressionLevel > 12 ||
      static_cast<uint8_t>(value.predictorMode) >
        static_cast<uint8_t>(PaqPredictorMode::LSTM_ONLY) ||
      (value.optionFlags & ~kKnownPaqOptionFlags) != 0 ||
      value.modelMemoryClass != 0 ||
      (value.tuningParameterBits & UINT32_C(0x7f800000)) ==
        UINT32_C(0x7f800000))
    return false;

  const bool useLstm = hasPaqOption(value.optionFlags,
                                    PaqOptionFlag::USE_LSTM);
  // The legacy -E/-T modes depend on the running executable or external
  // dictionary files.  Native v1 has no resource-bundle contract, so these
  // flags must remain on the complete legacy PAQ path as well.
  if (hasPaqOption(value.optionFlags, PaqOptionFlag::TRAIN_EXE) ||
      hasPaqOption(value.optionFlags, PaqOptionFlag::TRAIN_TEXT) || useLstm)
    return false;
  if (!useLstm) {
    if (value.predictorMode != PaqPredictorMode::CONTEXT_MIXING ||
        value.compressionLevel == 0 || value.lstmLayers != 0 ||
        value.lstmHiddenSize != 0 || value.lstmHorizon != 0 ||
        !paq_config_detail::allZero(value.lstmWeightDigest))
      return false;
  }
  return true;
}

inline std::vector<uint8_t> encodePaqConfigCanonical(
    const PaqConfigV1& value) {
  if (!validPaqConfigV1(value))
    quit("Cannot encode an invalid PAQ decoder configuration.");

  std::vector<uint8_t> output;
  output.reserve(kPaqConfigV1CanonicalSize);
  output.insert(output.end(), paq_config_detail::kMagic.begin(),
                paq_config_detail::kMagic.end());
  paq_config_detail::append16(output, value.configRevision);
  paq_config_detail::append16(output, value.paqCoreRevision);
  paq_config_detail::append16(output, value.arithmeticRevision);
  paq_config_detail::append16(output, value.blockStreamRevision);
  paq_config_detail::append16(output, value.transformTableRevision);
  paq_config_detail::append16(output, value.modelRuleRevision);
  output.push_back(value.compressionLevel);
  output.push_back(static_cast<uint8_t>(value.predictorMode));
  paq_config_detail::append16(output, value.optionFlags);
  paq_config_detail::append32(output, value.modelMemoryClass);
  output.push_back(value.lstmLayers);
  output.push_back(value.lstmHiddenSize);
  output.push_back(value.lstmHorizon);
  output.push_back(0); // Reserved; canonical encoders must write zero.
  paq_config_detail::append32(output, value.tuningParameterBits);
  output.insert(output.end(), value.lstmWeightDigest.begin(),
                value.lstmWeightDigest.end());
  if (output.size() != kPaqConfigV1CanonicalSize)
    quit("Internal PAQ decoder configuration size mismatch.");
  return output;
}

inline bool decodePaqConfigCanonical(const std::vector<uint8_t>& bytes,
                                     PaqConfigV1& value) {
  if (bytes.size() != kPaqConfigV1CanonicalSize)
    return false;
  for (size_t index = 0; index < paq_config_detail::kMagic.size(); ++index) {
    if (bytes[index] != paq_config_detail::kMagic[index])
      return false;
  }

  size_t position = paq_config_detail::kMagic.size();
  value.configRevision = paq_config_detail::load16(bytes.data() + position);
  position += 2;
  value.paqCoreRevision = paq_config_detail::load16(bytes.data() + position);
  position += 2;
  value.arithmeticRevision = paq_config_detail::load16(bytes.data() + position);
  position += 2;
  value.blockStreamRevision = paq_config_detail::load16(bytes.data() + position);
  position += 2;
  value.transformTableRevision =
    paq_config_detail::load16(bytes.data() + position);
  position += 2;
  value.modelRuleRevision = paq_config_detail::load16(bytes.data() + position);
  position += 2;
  value.compressionLevel = bytes[position++];
  value.predictorMode = static_cast<PaqPredictorMode>(bytes[position++]);
  value.optionFlags = paq_config_detail::load16(bytes.data() + position);
  position += 2;
  value.modelMemoryClass = paq_config_detail::load32(bytes.data() + position);
  position += 4;
  value.lstmLayers = bytes[position++];
  value.lstmHiddenSize = bytes[position++];
  value.lstmHorizon = bytes[position++];
  if (bytes[position++] != 0)
    return false;
  value.tuningParameterBits =
    paq_config_detail::load32(bytes.data() + position);
  position += 4;
  for (size_t index = 0; index < value.lstmWeightDigest.size(); ++index)
    value.lstmWeightDigest[index] = bytes[position++];
  return position == bytes.size() && validPaqConfigV1(value);
}

inline PaqConfigId makePaqConfigId(
    const std::vector<uint8_t>& canonicalConfig) {
  PaqConfigV1 parsed;
  if (!decodePaqConfigCanonical(canonicalConfig, parsed))
    quit("Cannot identify a non-canonical PAQ decoder configuration.");

  PaqConfigId result{};
  for (uint64_t domain = 0; domain < 4; ++domain) {
    const uint64_t hash =
      paq_config_detail::domainFnv1a(canonicalConfig, domain + 1);
    paq_config_detail::store64(result.data() + domain * 8, hash);
  }
  return result;
}

inline PaqConfigId makePaqConfigId(const PaqConfigV1& value) {
  return makePaqConfigId(encodePaqConfigCanonical(value));
}

inline bool isZeroPaqConfigId(const PaqConfigId& value) {
  return paq_config_detail::allZero(value);
}

inline bool canonicalPaqConfigEquals(const std::vector<uint8_t>& left,
                                     const std::vector<uint8_t>& right) {
  // The identifier is only an index/fast reject.  These complete canonical
  // bytes are the authoritative state-compatibility comparison.
  return left == right;
}

} // namespace routed
