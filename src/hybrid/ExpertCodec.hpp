#pragma once

#include "../Utils.hpp"
#include "../file/File.hpp"
#include "ProfileTypes.hpp"

#include <cstdint>
#include <limits>
#include <vector>

namespace routed {

enum class ExpertEncodeStatus : uint8_t {
  OK = 0,
  NOT_APPLICABLE,
  INVALID_INPUT,
  RESOURCE_LIMIT,
  ENCODE_FAILED,
  VERIFY_FAILED
};

enum class ExpertDecodeStatus : uint8_t {
  OK = 0,
  INVALID_PARAMETERS,
  CORRUPT_PAYLOAD,
  RESOURCE_LIMIT,
  LENGTH_MISMATCH,
  UNSUPPORTED_CONTRACT
};

struct ExpertLimits {
  uint64_t maximumDecodedBytes = UINT64_C(1) << 40;
  uint64_t maximumPayloadBytes = UINT64_C(1) << 40;
  uint64_t maximumResidentBytes = UINT64_C(512) << 20;
  uint32_t maximumRecursionDepth = 16;
};

struct ExpertContract {
  ExpertId id = ExpertId::PAQ_LEGACY_ARCHIVE;
  uint16_t revision = 0;
  uint16_t decoderContractVersion = 0;
  bool selfContainedPayload = true;
  bool requiresReconstructionRecipe = false;
};

class ExpertCodec {
public:
  virtual ~ExpertCodec() = default;
  virtual ExpertContract contract() const = 0;

  // This is a resource bound, not a promise that the selected expert wins on
  // size. Routing never runs PAQ or another expert for a post-encode race.
  virtual bool maximumPayloadBytes(
      uint64_t decodedLength,
      const std::vector<uint8_t>& canonicalParameters,
      uint64_t& result) const = 0;

  virtual ExpertEncodeStatus encode(
      File* boundedDecodedInput,
      uint64_t decodedLength,
      const std::vector<uint8_t>& canonicalParameters,
      const ExpertLimits& limits,
      File* payloadOutput) const = 0;

  virtual ExpertDecodeStatus decode(
      File* boundedPayloadInput,
      uint64_t payloadLength,
      const std::vector<uint8_t>& canonicalParameters,
      uint64_t expectedDecodedLength,
      const ExpertLimits& limits,
      File* decodedOutput) const = 0;
};

class ExpertRegistry {
public:
  void add(const ExpertCodec* codec) {
    if (codec == nullptr || codec->contract().revision == 0 ||
        codec->contract().decoderContractVersion == 0)
      quit("Cannot register an invalid routed expert contract.");
    if (find(codec->contract().id, codec->contract().revision) != nullptr)
      quit("Routed expert contract is registered more than once.");
    codecs_.push_back(codec);
  }

  const ExpertCodec* find(ExpertId id, uint16_t revision) const {
    for (const ExpertCodec* codec : codecs_) {
      const ExpertContract contract = codec->contract();
      if (contract.id == id && contract.revision == revision)
        return codec;
    }
    return nullptr;
  }

private:
  std::vector<const ExpertCodec*> codecs_;
};

} // namespace routed
