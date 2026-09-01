#pragma once

#include "ExpertCodec.hpp"
#include "OpenZlPlanRegistry.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#if defined(PAQ_ENABLE_OPENZL)
#include <openzl/openzl.h>
#include <openzl/zl_decompress.h>
#include <openzl/zl_version.h>
#include <openzl/codecs/zl_ace.h>
#include <openzl/codecs/zl_conversion.h>
#include <openzl/codecs/zl_delta.h>
#include <openzl/codecs/zl_field_lz.h>
#include <openzl/codecs/zl_illegal.h>
#include <openzl/codecs/zl_split.h>
#include <openzl/codecs/zl_split_by_struct.h>
#include <openzl/codecs/zl_store.h>
#include <openzl/codecs/zl_tokenize.h>
#include <openzl/codecs/zl_transpose.h>
#include <openzl/codecs/zl_zstd.h>

#if ZL_LIBRARY_VERSION_NUMBER != 200
#error "PAQ routed OpenZL v1 requires exactly OpenZL v0.2.0."
#endif
#endif

namespace routed {

namespace openzl_expert_detail {

constexpr uint64_t kPayloadFixedAllowance = UINT64_C(4) << 20;
constexpr uint64_t kPayloadExpansionFactor = 4;

inline bool checkedPayloadBound(uint64_t decodedLength, uint64_t& result) {
  if (decodedLength >
      (std::numeric_limits<uint64_t>::max() - kPayloadFixedAllowance) /
        kPayloadExpansionFactor)
    return false;
  result = decodedLength * kPayloadExpansionFactor + kPayloadFixedAllowance;
  return true;
}

inline bool checkedResidentNeed(uint64_t decodedLength, uint64_t payloadBound,
                                uint64_t multiplier, uint64_t& result) {
  if (decodedLength > std::numeric_limits<uint64_t>::max() / multiplier)
    return false;
  const uint64_t decodedBuffers = decodedLength * multiplier;
  if (decodedBuffers > std::numeric_limits<uint64_t>::max() - payloadBound)
    return false;
  result = decodedBuffers + payloadBound;
  return true;
}

inline bool readVectorExact(File* input, uint64_t length,
                            std::vector<uint8_t>& bytes) {
  if (input == nullptr || length > std::numeric_limits<size_t>::max())
    return false;
  bytes.resize(static_cast<size_t>(length));
  return length == 0 || input->blockRead(bytes.data(), length) == length;
}

inline void writeVector(File* output, std::vector<uint8_t>& bytes) {
  if (output == nullptr)
    quit("OpenZL output is unavailable.");
  if (!bytes.empty())
    output->blockWrite(bytes.data(), bytes.size());
}

#if defined(PAQ_ENABLE_OPENZL)

struct OpenZlCompressorOwner {
  ZL_Compressor* value = nullptr;
  ~OpenZlCompressorOwner() { ZL_Compressor_free(value); }
};

struct OpenZlCCtxOwner {
  ZL_CCtx* value = nullptr;
  ~OpenZlCCtxOwner() { ZL_CCtx_free(value); }
};

inline bool setCParam(ZL_CCtx* context, ZL_CParam parameter, int value) {
  return !ZL_isError(ZL_CCtx_setParameter(context, parameter, value));
}

inline ZL_GraphID huffmanGraph() {
  return ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_huffman);
}

inline ZL_GraphID staticOneOutput(ZL_Compressor* compressor,
                                  ZL_NodeID node,
                                  ZL_GraphID successor) {
  return ZL_Compressor_registerStaticGraph_fromNode1o(
    compressor, node, successor);
}

inline ZL_GraphID aceWithFrozenDefault(ZL_Compressor* compressor,
                                      ZL_GraphID defaultGraph) {
  if (!ZL_GraphID_isValid(defaultGraph))
    return ZL_GRAPH_ILLEGAL;
  return ZL_Compressor_buildACEGraphWithDefault(compressor, defaultGraph);
}

// Immutable PlanId 0x00010001 reproduces the published OpenZL v0.2.0 SAO
// profile.  The 28-byte catalog header is an explicit tiny internal STORE;
// this is framing metadata, not an outer RAW/STORE escape. All automatic
// min-stream, permissive and store-on-expansion fallbacks are disabled below.
inline bool buildFrozenSaoCompressor(OpenZlCompressorOwner& owner) {
  owner.value = ZL_Compressor_create();
  if (owner.value == nullptr)
    return false;

  if (ZL_isError(ZL_Compressor_setParameter(
        owner.value, ZL_CParam_compressionLevel, 1)))
    return false;

  const ZL_GraphID sraDelta = staticOneOutput(
    owner.value, ZL_NODE_DELTA_INT, ZL_GRAPH_FIELD_LZ);
  const ZL_GraphID sraNumeric = staticOneOutput(
    owner.value, ZL_NODE_CONVERT_STRUCT_TO_NUM_LE, sraDelta);
  const ZL_GraphID sra0 = aceWithFrozenDefault(owner.value, sraNumeric);

  const ZL_GraphID sdecTranspose =
    ZL_Compressor_registerTransposeSplitGraph(owner.value, ZL_GRAPH_ZSTD);
  const ZL_GraphID sdec0 =
    aceWithFrozenDefault(owner.value, sdecTranspose);

  const ZL_GraphID structToken = ZL_Compressor_registerTokenizeGraph(
    owner.value, ZL_Type_struct, false,
    ZL_GRAPH_FIELD_LZ, ZL_GRAPH_FIELD_LZ);
  const ZL_GraphID numericToken = ZL_Compressor_registerTokenizeGraph(
    owner.value, ZL_Type_numeric, false, huffmanGraph(), huffmanGraph());
  const ZL_GraphID numericTokenFromStruct = staticOneOutput(
    owner.value, ZL_NODE_CONVERT_STRUCT_TO_NUM_LE, numericToken);

  const ZL_GraphID isGraph =
    aceWithFrozenDefault(owner.value, numericTokenFromStruct);
  const ZL_GraphID magGraph =
    aceWithFrozenDefault(owner.value, numericTokenFromStruct);
  const ZL_GraphID xrpmGraph =
    aceWithFrozenDefault(owner.value, structToken);
  const ZL_GraphID xdpmGraph =
    aceWithFrozenDefault(owner.value, structToken);

  const size_t fieldSizes[] = {8, 8, 2, 2, 4, 4};
  const ZL_GraphID fieldGraphs[] = {
    sra0, sdec0, isGraph, magGraph, xrpmGraph, xdpmGraph
  };
  const ZL_GraphID recordGraph = ZL_Compressor_registerSplitByStructGraph(
    owner.value, fieldSizes, fieldGraphs,
    sizeof(fieldSizes) / sizeof(fieldSizes[0]));
  if (!ZL_GraphID_isValid(recordGraph))
    return false;

  const size_t topSizes[] = {kSaoSilesiaHeaderBytes, 0};
  const ZL_GraphID topGraphs[] = {ZL_GRAPH_STORE, recordGraph};
  const ZL_GraphID root = ZL_Compressor_registerSplitGraph(
    owner.value, ZL_Type_serial, topSizes, topGraphs,
    sizeof(topSizes) / sizeof(topSizes[0]));
  if (!ZL_GraphID_isValid(root))
    return false;
  return !ZL_isError(ZL_Compressor_selectStartingGraphID(owner.value, root));
}

inline bool configureFrozenContext(ZL_CCtx* context,
                                   const ZL_Compressor* compressor) {
  if (context == nullptr || compressor == nullptr ||
      ZL_isError(ZL_CCtx_refCompressor(context, compressor)))
    return false;
  return setCParam(context, ZL_CParam_formatVersion, kOpenZlWireRevision) &&
         setCParam(context, ZL_CParam_permissiveCompression,
                   ZL_TernaryParam_disable) &&
         setCParam(context, ZL_CParam_compressedChecksum,
                   ZL_TernaryParam_enable) &&
         setCParam(context, ZL_CParam_contentChecksum,
                   ZL_TernaryParam_enable) &&
         setCParam(context, ZL_CParam_minStreamSize, -1) &&
         setCParam(context, ZL_CParam_storeOnExpansion,
                   ZL_TernaryParam_disable);
}

inline bool decodeBytes(const std::vector<uint8_t>& payload,
                        uint64_t expectedLength,
                        std::vector<uint8_t>& decoded) {
  if (expectedLength > std::numeric_limits<size_t>::max() || payload.empty())
    return false;
  const ZL_Report wireVersion = ZL_getFormatVersionFromFrame(
    payload.data(), payload.size());
  if (ZL_isError(wireVersion) ||
      ZL_validResult(wireVersion) != kOpenZlWireRevision)
    return false;
  const ZL_Report declaredSize = ZL_getDecompressedSize(
    payload.data(), payload.size());
  if (ZL_isError(declaredSize) ||
      ZL_validResult(declaredSize) != expectedLength)
    return false;
  // OpenZL frames self-describe the decoder graph.  v0.2.0 exposes no public
  // pre-decode API that authenticates that graph against our frozen PlanId.
  // The outer parameters therefore freeze/admit the encoder contract, while
  // wire revision, declared size, OpenZL checksums and the outer decoded CRC
  // bound and authenticate the actual decode result.  Do not describe the
  // PlanId fingerprint as a pre-decode frame-graph fingerprint.
  decoded.resize(static_cast<size_t>(expectedLength));
  const ZL_Report result = ZL_decompress(
    decoded.data(), decoded.size(), payload.data(), payload.size());
  return !ZL_isError(result) && ZL_validResult(result) == decoded.size();
}

#endif

} // namespace openzl_expert_detail

class OpenZlFrozenExpert final : public ExpertCodec {
public:
  ExpertContract contract() const override {
    return {ExpertId::OPENZL_FROZEN_V1, kOpenZlFrozenExpertRevision,
            kOpenZlDecoderContractVersion, true, false};
  }

  bool maximumPayloadBytes(
      uint64_t decodedLength,
      const std::vector<uint8_t>& canonicalParameters,
      uint64_t& result) const override {
    OpenZlFrozenParamsV1 params;
    return decodeOpenZlFrozenParams(canonicalParameters, decodedLength, params) &&
           openzl_expert_detail::checkedPayloadBound(decodedLength, result);
  }

  ExpertEncodeStatus encode(
      File* boundedDecodedInput, uint64_t decodedLength,
      const std::vector<uint8_t>& canonicalParameters,
      const ExpertLimits& limits, File* payloadOutput) const override {
    OpenZlFrozenParamsV1 params;
    if (!decodeOpenZlFrozenParams(canonicalParameters, decodedLength, params))
      return ExpertEncodeStatus::INVALID_INPUT;
    if (decodedLength > limits.maximumDecodedBytes)
      return ExpertEncodeStatus::RESOURCE_LIMIT;
#if !defined(PAQ_ENABLE_OPENZL)
    (void)boundedDecodedInput;
    (void)payloadOutput;
    return ExpertEncodeStatus::NOT_APPLICABLE;
#else
    uint64_t payloadBound = 0;
    uint64_t residentNeed = 0;
    if (!openzl_expert_detail::checkedPayloadBound(decodedLength, payloadBound) ||
        payloadBound > limits.maximumPayloadBytes ||
        payloadBound > std::numeric_limits<size_t>::max() ||
        !openzl_expert_detail::checkedResidentNeed(
          decodedLength, payloadBound, 2, residentNeed) ||
        residentNeed > limits.maximumResidentBytes)
      return ExpertEncodeStatus::RESOURCE_LIMIT;

    std::vector<uint8_t> source;
    if (!openzl_expert_detail::readVectorExact(
          boundedDecodedInput, decodedLength, source))
      return ExpertEncodeStatus::INVALID_INPUT;

    openzl_expert_detail::OpenZlCompressorOwner compressor;
    if (!openzl_expert_detail::buildFrozenSaoCompressor(compressor))
      return ExpertEncodeStatus::ENCODE_FAILED;
    openzl_expert_detail::OpenZlCCtxOwner context;
    context.value = ZL_CCtx_create();
    if (!openzl_expert_detail::configureFrozenContext(
          context.value, compressor.value))
      return ExpertEncodeStatus::ENCODE_FAILED;

    std::vector<uint8_t> payload(static_cast<size_t>(payloadBound));
    const ZL_Report result = ZL_CCtx_compress(
      context.value, payload.data(), payload.size(), source.data(), source.size());
    if (ZL_isError(result))
      return ExpertEncodeStatus::ENCODE_FAILED;
    const size_t payloadSize = ZL_validResult(result);
    if (payloadSize > payload.size())
      return ExpertEncodeStatus::ENCODE_FAILED;
    payload.resize(payloadSize);
    if (payload.size() > limits.maximumPayloadBytes)
      return ExpertEncodeStatus::RESOURCE_LIMIT;

    std::vector<uint8_t> verification;
    if (!openzl_expert_detail::decodeBytes(
          payload, decodedLength, verification) || verification != source)
      return ExpertEncodeStatus::VERIFY_FAILED;
    openzl_expert_detail::writeVector(payloadOutput, payload);
    return ExpertEncodeStatus::OK;
#endif
  }

  ExpertDecodeStatus decode(
      File* boundedPayloadInput, uint64_t payloadLength,
      const std::vector<uint8_t>& canonicalParameters,
      uint64_t expectedDecodedLength, const ExpertLimits& limits,
      File* decodedOutput) const override {
    OpenZlFrozenParamsV1 params;
    if (!decodeOpenZlFrozenParams(
          canonicalParameters, expectedDecodedLength, params))
      return ExpertDecodeStatus::INVALID_PARAMETERS;
    if (payloadLength > limits.maximumPayloadBytes ||
        expectedDecodedLength > limits.maximumDecodedBytes)
      return ExpertDecodeStatus::RESOURCE_LIMIT;
#if !defined(PAQ_ENABLE_OPENZL)
    (void)boundedPayloadInput;
    (void)decodedOutput;
    return ExpertDecodeStatus::UNSUPPORTED_CONTRACT;
#else
    uint64_t residentNeed = 0;
    if (payloadLength > std::numeric_limits<size_t>::max() ||
        !openzl_expert_detail::checkedResidentNeed(
          expectedDecodedLength, payloadLength, 1, residentNeed) ||
        residentNeed > limits.maximumResidentBytes)
      return ExpertDecodeStatus::RESOURCE_LIMIT;
    std::vector<uint8_t> payload;
    if (!openzl_expert_detail::readVectorExact(
          boundedPayloadInput, payloadLength, payload))
      return ExpertDecodeStatus::CORRUPT_PAYLOAD;
    std::vector<uint8_t> decoded;
    if (!openzl_expert_detail::decodeBytes(
          payload, expectedDecodedLength, decoded))
      return ExpertDecodeStatus::CORRUPT_PAYLOAD;
    if (decoded.size() != expectedDecodedLength)
      return ExpertDecodeStatus::LENGTH_MISMATCH;
    openzl_expert_detail::writeVector(decodedOutput, decoded);
    return ExpertDecodeStatus::OK;
#endif
  }
};

} // namespace routed
