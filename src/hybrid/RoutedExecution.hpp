#pragma once

#include "../Encoder.hpp"
#include "../Shared.hpp"
#include "../String.hpp"
#include "../TransformOptions.hpp"
#include "../file/FileDisk.hpp"
#include "../file/FileTmp.hpp"
#include "../filter/BlockPlan.hpp"
#include "../filter/Filters.hpp"
#include "ContainerLayout.hpp"
#include "ExecutableLayout.hpp"
#include "ExpertCodec.hpp"
#include "FrozenRuleSet.hpp"
#include "OpenZlExpert.hpp"
#include "OpenZlPlanRegistry.hpp"
#include "PaqConfig.hpp"
#include "PaqModelSession.hpp"
#include "RoutedArchive.hpp"
#include "RoutedIO.hpp"
#include "RoutingPlan.hpp"
#include "SaoSchemaParser.hpp"
#include "ZipParser.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace routed {

struct NativeRoutedStats {
  uint64_t sourceBytes = 0;
  uint64_t archiveBytes = 0;
  uint32_t segmentCount = 0;
  uint32_t paqFragmentCount = 0;
  uint32_t externalExpertCount = 0;
  uint64_t firstDifference = 0; // One based; zero means no difference.
  bool comparedFileIsLonger = false;
};

namespace routed_execution_detail {

constexpr uint32_t kPaqStateId = 1;
constexpr uint32_t kMaximumPlanningDepth = 16;
constexpr uint64_t kMaximumCommitBytes = kMaximumRoutedFieldLength;
constexpr uint64_t kMaximumPaqFragmentDecodedBytes = UINT64_C(1) << 30;
constexpr uint64_t kMaximumPaqFragmentPayloadBytes =
  (UINT64_C(1) << 31) - 1;

inline uint64_t fileLengthPreservingPosition(File* file) {
  if (file == nullptr)
    quit("Native routed file is unavailable.");
  const uint64_t saved = file->curPos();
  file->setEnd();
  const uint64_t length = file->curPos();
  file->setpos(saved);
  return length;
}

inline PaqConfigV1 paqConfigFromShared(const Shared& shared) {
  PaqConfigV1 config;
  config.compressionLevel = shared.level;
  if (shared.GetOptionTrainExe())
    config.optionFlags |= paqOptionFlag(PaqOptionFlag::TRAIN_EXE);
  if (shared.GetOptionTrainTxt())
    config.optionFlags |= paqOptionFlag(PaqOptionFlag::TRAIN_TEXT);
  if (shared.GetOptionAdaptiveLearningRate())
    config.optionFlags |=
      paqOptionFlag(PaqOptionFlag::ADAPTIVE_LEARNING_RATE);
  if (shared.GetOptionSkipRGB())
    config.optionFlags |= paqOptionFlag(PaqOptionFlag::SKIP_RGB_TRANSFORM);
  if (shared.GetOptionUseLSTM()) {
    config.optionFlags |= paqOptionFlag(PaqOptionFlag::USE_LSTM);
    config.lstmLayers = shared.LstmSettings.num_layers;
    config.lstmHiddenSize = shared.LstmSettings.hidden_size;
    config.lstmHorizon = shared.LstmSettings.horizon;
    if (shared.level == 0)
      config.predictorMode = PaqPredictorMode::LSTM_ONLY;
  }
  static_assert(sizeof(config.tuningParameterBits) == sizeof(shared.tuning_param),
                "PAQ tuning parameter must remain binary32.");
  std::memcpy(&config.tuningParameterBits, &shared.tuning_param,
              sizeof(config.tuningParameterBits));
  if (!validPaqConfigV1(config))
    quit("Current PAQ options cannot be represented by native routed v1.");
  return config;
}

inline void applyPaqConfig(const PaqConfigV1& config, Shared& shared) {
  if (!validPaqConfigV1(config))
    quit("Routed PAQ configuration is unsupported by this decoder.");
  shared.options = 0;
  if (hasPaqOption(config.optionFlags, PaqOptionFlag::TRAIN_EXE))
    shared.SetOptionTrainExe();
  if (hasPaqOption(config.optionFlags, PaqOptionFlag::TRAIN_TEXT))
    shared.SetOptionTrainTxt();
  if (hasPaqOption(config.optionFlags,
                   PaqOptionFlag::ADAPTIVE_LEARNING_RATE))
    shared.SetOptionAdaptiveLearningRate();
  if (hasPaqOption(config.optionFlags, PaqOptionFlag::SKIP_RGB_TRANSFORM))
    shared.SetOptionSkipRGB();
  if (hasPaqOption(config.optionFlags, PaqOptionFlag::USE_LSTM))
    shared.SetOptionUseLSTM();
  shared.LstmSettings.num_layers = config.lstmLayers;
  shared.LstmSettings.hidden_size = config.lstmHiddenSize;
  shared.LstmSettings.horizon = config.lstmHorizon;
  std::memcpy(&shared.tuning_param, &config.tuningParameterBits,
              sizeof(shared.tuning_param));
  // Native routed v1 freezes the scalar PAQ implementation. SIMD selection
  // is intentionally not host-dependent archive state.
  shared.chosenSimd = SIMDType::SIMD_NONE;
  shared.init(config.compressionLevel);
}

inline void appendPlannedBlock(BlockPlan& destination,
                               const PlannedBlock& block) {
  destination.append(block.sourceOffset, block.sourceLength, block.legacyType,
                     block.blockInfo, block.profileDecision, block.route);
}

inline void appendPlan(BlockPlan& destination, const BlockPlan& source) {
  source.validateForEncoding();
  for (size_t index = 0; index < source.size(); ++index)
    appendPlannedBlock(destination, source[index]);
}

struct PreparedPlan {
  explicit PreparedPlan(uint64_t sourceLength)
      : blocks(new BlockPlan(0, sourceLength)) {}

  std::unique_ptr<BlockPlan> blocks;
  CommitPlan commits;
  std::vector<std::unique_ptr<FileTmp>> preparedExternalPayloads;
};

inline void validatePreparedPlan(const PreparedPlan& prepared,
                                 uint64_t sourceLength) {
  prepared.blocks->validateForEncoding();
  prepared.commits.validateSealed();
  if (prepared.commits.size() > kMaximumRoutedSegments)
    quit("Native routed plan exceeds the segment-count limit.");

  size_t expectedBlock = 0;
  for (size_t commitIndex = 0;
       commitIndex < prepared.commits.size(); ++commitIndex) {
    const CommitUnit& commit = prepared.commits.at(commitIndex);
    if (expectedBlock > prepared.blocks->size() ||
        commit.firstBlockIndex != expectedBlock ||
        commit.blockCount > prepared.blocks->size() - expectedBlock)
      quit("Routed commit references a noncanonical block-plan slice.");
    uint64_t expectedOffset = commit.sourceOffset;
    uint64_t covered = 0;
    for (size_t local = 0; local < commit.blockCount; ++local) {
      const PlannedBlock& block = (*prepared.blocks)[expectedBlock + local];
      if (block.sourceOffset != expectedOffset ||
          covered > commit.sourceLength ||
          block.sourceLength > commit.sourceLength - covered)
        quit("Routed commit block slice disagrees with its source range.");
      expectedOffset += block.sourceLength;
      covered += block.sourceLength;
    }
    if (covered != commit.sourceLength)
      quit("Routed commit block slice does not cover its source range.");
    expectedBlock += commit.blockCount;
  }
  if (expectedBlock != prepared.blocks->size() ||
      prepared.commits.totalSourceLength() != sourceLength)
    quit("Routed commit and block plans do not describe the same source.");
}

class SourcePlanner {
public:
  SourcePlanner(File* source, uint64_t sourceLength, const Shared* shared,
                const FrozenRoutingFeatures& features)
      : source_(source), sourceLength_(sourceLength), shared_(shared),
        features_(features), transformOptions_(shared), result_(sourceLength) {
    if (source_ == nullptr || shared_ == nullptr ||
        sourceLength_ > kMaximumRoutedFieldLength)
      quit("Invalid native routed planning input.");
    experts_.add(&openZl_);
  }

  PreparedPlan take() {
    if (sourceLength_ == 0)
      appendEmptyCommit();
    else if (shared_->GetOptionDetectBlockAsBinary())
      appendForcedPaqRange(0, sourceLength_, BlockType::DEFAULT, 0);
    else if (shared_->GetOptionDetectBlockAsText())
      appendForcedPaqRange(0, sourceLength_, BlockType::TEXT, -1);
    else
      planRange(0, sourceLength_, 0);

    result_.blocks->seal();
    if (result_.preparedExternalPayloads.size() != result_.commits.size())
      quit("Routed prepared-payload table disagrees with the commit plan.");
    result_.commits.seal(sourceLength_);
    return std::move(result_);
  }

private:
  void appendEmptyCommit() {
    CommitUnit commit;
    commit.commitId = 0;
    commit.sourceOffset = 0;
    commit.sourceLength = 0;
    commit.firstBlockIndex = 0;
    commit.blockCount = 0;
    commit.requestedDecision = ProfileRegistry::nativePaqFallback();
    commit.decision = commit.requestedDecision;
    commit.routeFinalized = true;
    commit.stateFamily = StateFamily::PAQ_MODEL_SESSION_V1;
    commit.frames = makeTransportFrames(0, 0, 0, kMaximumCommitBytes);
    result_.commits.append(std::move(commit));
    result_.preparedExternalPayloads.emplace_back();
  }

  void appendCommit(uint64_t offset, uint64_t length, size_t firstBlock,
                    size_t blockCount, const RouteDecision& requested,
                    const RouteDecision& finalDecision,
                    StateFamily stateFamily,
                    std::unique_ptr<FileTmp> preparedPayload = nullptr) {
    if (result_.commits.size() >= kMaximumRoutedSegments)
      quit("Native routed plan exceeds the segment-count limit.");
    CommitUnit commit;
    commit.commitId = result_.commits.size();
    commit.sourceOffset = offset;
    commit.sourceLength = length;
    commit.firstBlockIndex = firstBlock;
    commit.blockCount = blockCount;
    commit.requestedDecision = requested;
    commit.decision = finalDecision;
    commit.routeFinalized = true;
    commit.stateFamily = stateFamily;
    commit.frames = makeTransportFrames(commit.commitId, offset, length,
                                        kMaximumCommitBytes);
    result_.commits.append(std::move(commit));
    result_.preparedExternalPayloads.push_back(std::move(preparedPayload));
  }

  void appendForcedPaqRange(uint64_t offset, uint64_t length,
                            BlockType type, int blockInfo,
                            ProfileId profile = ProfileId::PAQ_DEFAULT,
                            EvidenceKind evidence =
                              EvidenceKind::STATISTICAL_INFERENCE) {
    while (length != 0) {
      const uint64_t partLength = std::min<uint64_t>(
        length, kMaximumPaqFragmentDecodedBytes);
      const int partInfo = type == BlockType::EXE
        ? static_cast<int>(offset) : blockInfo;
      const size_t first = result_.blocks->size();
      RouteDecision descriptive = routeDecisionForLegacyType(type, partInfo);
      if (profile != descriptive.pipeline.profileId) {
        descriptive = ProfileRegistry::fallback();
        descriptive.pipeline = {profile, 1, 0};
        descriptive.evidence = evidence;
        descriptive.reasonCode = 0x90000000u |
          static_cast<uint32_t>(profile);
        descriptive.selected = true;
      }
      result_.blocks->append(offset, partLength, type, partInfo,
                             descriptive);
      const RouteDecision finalDecision =
        ProfileRegistry::paqFragmentFor(descriptive);
      appendCommit(offset, partLength, first, 1, descriptive, finalDecision,
                   StateFamily::PAQ_MODEL_SESSION_V1);
      offset += partLength;
      length -= partLength;
    }
  }

  void appendDetectedPaqRange(uint64_t offset, uint64_t length) {
    while (length != 0) {
      const uint64_t partLength = std::min<uint64_t>(
        length, kMaximumPaqFragmentDecodedBytes);
      if (offset > static_cast<uint64_t>(INT32_MAX) ||
          partLength > static_cast<uint64_t>(INT32_MAX) - offset) {
        appendForcedPaqRange(offset, partLength, BlockType::DEFAULT, 0);
      }
      else {
        source_->setpos(offset);
        const BlockPlan local =
          buildBlockPlan(source_, partLength, &transformOptions_);
        const size_t first = result_.blocks->size();
        appendPlan(*result_.blocks, local);
        for (size_t index = 0; index < local.size(); ++index) {
          const PlannedBlock& block = local[index];
          const RouteDecision finalDecision =
            ProfileRegistry::paqFragmentFor(block.profileDecision);
          appendCommit(block.sourceOffset, block.sourceLength, first + index, 1,
                       block.profileDecision, finalDecision,
                       StateFamily::PAQ_MODEL_SESSION_V1);
        }
      }
      offset += partLength;
      length -= partLength;
    }
  }

  bool appendOpenZlSao(uint64_t offset, uint64_t length) {
    if (!features_.openZlSao || !OpenZlPlanRegistry::encoderAvailable())
      return false;
    SaoSchemaMatch match;
    if (!detectSaoSilesiaFull(source_, offset, length, match) ||
        !OpenZlPlanRegistry::admitsSao(match))
      return false;

    const ExpertCodec* expert = experts_.find(
      ExpertId::OPENZL_FROZEN_V1, kOpenZlFrozenExpertRevision);
    if (expert == nullptr)
      return false;

    RouteDecision requested;
    requested.pipeline = {ProfileId::RECORD_SCHEMA_COLUMNAR, 1, 0};
    requested.evidence = EvidenceKind::EXACT_FORMAT;
    requested.statePolicy = StatePolicy::RESET;
    requested.expert = ExpertId::OPENZL_FROZEN_V1;
    requested.transform = TransformId::NONE;
    requested.parameters = encodeOpenZlFrozenParams(makeOpenZlSaoParams(match));
    requested.reasonCode = 0x53414f31u;
    requested.selected = true;

    std::unique_ptr<FileTmp> payload(new FileTmp());
    FileRangeView bounded(source_, offset, length);
    const ExpertEncodeStatus status = expert->encode(
      &bounded, length, requested.parameters, expertLimits_, payload.get());
    if (status != ExpertEncodeStatus::OK) {
      // Build the legacy PAQ fallback only when the admitted frozen expert
      // actually failed before commit. Successful external routing never pays
      // for a second detector/model candidate.
      source_->setpos(offset);
      const BlockPlan fallback =
        buildBlockPlan(source_, length, &transformOptions_);
      const size_t first = result_.blocks->size();
      appendPlan(*result_.blocks, fallback);
      const RouteDecision fallbackDecision =
        ProfileRegistry::paqFragmentFor(requested);
      appendCommit(offset, length, first, fallback.size(), requested,
                   fallbackDecision, StateFamily::PAQ_MODEL_SESSION_V1);
      return true;
    }

    // BlockPlan is an encoder-side exact-coverage ledger even for an external
    // leaf. One descriptive block records the already proven SAO object; it is
    // not encoded by PAQ and does not run the legacy detector.
    const size_t first = result_.blocks->size();
    result_.blocks->append(offset, length, BlockType::DEFAULT, 0, requested);
    appendCommit(offset, length, first, 1, requested, requested,
                  StateFamily::NONE, std::move(payload));
    return true;
  }

  bool appendExecutable(uint64_t offset, uint64_t length) {
    if (!features_.exactX86ExecutableSections)
      return false;
    ExecutableLayout layout;
    if (!detectExecutableLayout(source_, offset, length, layout) ||
        !layout.validCoverage())
      return false;
    for (const ExecutableSpan& span : layout.spans()) {
      if (span.isCode() && span.sourceOffset <=
            static_cast<uint64_t>(INT32_MAX) - 6 &&
          span.sourceLength <= static_cast<uint64_t>(INT32_MAX) -
            span.sourceOffset - 6) {
        appendForcedPaqRange(span.sourceOffset, span.sourceLength,
                             BlockType::EXE,
                             static_cast<int>(span.sourceOffset),
                             ProfileId::MACHINE_CODE,
                             EvidenceKind::EXACT_FORMAT);
      }
      else {
        // The executable parser has already classified this exact range. Do
        // not run the heuristic EXE detector again and accidentally apply BCJ
        // a second time to resources, headers, overlays or oversized sections.
        appendForcedPaqRange(span.sourceOffset, span.sourceLength,
                             BlockType::DEFAULT, 0);
      }
    }
    return true;
  }

  bool appendZip(uint64_t offset, uint64_t length, uint32_t depth) {
    if (!features_.zipStoredMembers)
      return false;
    ContainerLayout layout;
    const ZipParseStatus status = parseZipLayout(
      source_, offset, length, layout, nullptr, zipLimits_);
    if (status != ZipParseStatus::OK)
      return false;
    if (!layout.validCoverage())
      quit("Sealed ZIP layout lost exact source coverage.");
    for (const ContainerSpan& span : layout.spans()) {
      if (span.recursivelyPlannable())
        planRange(span.sourceOffset, span.sourceLength, depth + 1);
      else if (span.kind == ContainerSpanKind::STRUCTURE)
        appendForcedPaqRange(span.sourceOffset, span.sourceLength,
                             BlockType::DEFAULT, 0,
                             ProfileId::ZIP_CONTAINER_STRUCTURE,
                             EvidenceKind::EXACT_FORMAT);
      else
        appendForcedPaqRange(span.sourceOffset, span.sourceLength,
                             BlockType::DEFAULT, 0);
    }
    return true;
  }

  void planRange(uint64_t offset, uint64_t length, uint32_t depth) {
    if (length == 0)
      return;
    // A parser/resource limit is a routing rejection, not a fatal property of
    // otherwise valid input. At the frozen nesting limit, stop opening stored
    // ZIP members and let the complete range use the normal PAQ path.
    if (depth >= kMaximumPlanningDepth) {
      appendDetectedPaqRange(offset, length);
      return;
    }
    if (appendZip(offset, length, depth))
      return;
    if (appendExecutable(offset, length))
      return;
    if (appendOpenZlSao(offset, length))
      return;
    appendDetectedPaqRange(offset, length);
  }

  File* source_ = nullptr;
  uint64_t sourceLength_ = 0;
  const Shared* shared_ = nullptr;
  FrozenRoutingFeatures features_;
  TransformOptions transformOptions_;
  PreparedPlan result_;
  OpenZlFrozenExpert openZl_;
  ExpertRegistry experts_;
  ExpertLimits expertLimits_;
  ZipParseLimits zipLimits_;
};

inline uint16_t paqSegmentFlags(size_t commitIndex,
                                size_t firstPaqCommit,
                                size_t lastPaqCommit) {
  uint16_t flags = commitIndex == firstPaqCommit
    ? segmentFlag(SegmentFlag::START)
    : segmentFlag(SegmentFlag::CONTINUE);
  if (commitIndex == lastPaqCommit)
    flags |= segmentFlag(SegmentFlag::END);
  return flags;
}

inline RoutedSegmentHeader makeSegmentHeader(
    const CommitUnit& commit, uint64_t segmentId,
    const RangeIdentity& decodedIdentity, const RangeIdentity& payloadIdentity,
    const std::vector<uint8_t>& parameters,
    const std::vector<uint8_t>& recipe, uint16_t flags,
    uint64_t stateCompatibility) {
  RoutedSegmentHeader header;
  header.kind = SegmentKind::ROUTED_PROFILE;
  header.evidence = commit.decision.evidence;
  header.statePolicy = commit.decision.statePolicy;
  header.pipeline = commit.decision.pipeline;
  header.expert = commit.decision.expert;
  header.transform = TransformId::NONE;
  header.flags = flags;
  header.decoderContractVersion = commit.decision.expert ==
      ExpertId::OPENZL_FROZEN_V1
    ? kOpenZlDecoderContractVersion : 1;
  header.stateId = commit.decision.expert ==
      ExpertId::PAQ_BLOCK_FRAGMENT_V1 ? kPaqStateId : 0;
  header.reasonCode = commit.decision.reasonCode;
  header.scoreQ8 = commit.decision.scoreQ8;
  header.predictedSavingBytes = commit.decision.predictedSavingBytes;
  header.decodedLength = decodedIdentity.length;
  header.payloadLength = payloadIdentity.length;
  header.parameterLength = parameters.size();
  header.recipeLength = recipe.size();
  header.stateCompatibility = stateCompatibility;
  header.decodedCrc32c = decodedIdentity.crc32c;
  header.payloadCrc32c = payloadIdentity.crc32c;
  header.parameterCrc32c = crc32c(parameters.data(), parameters.size());
  header.recipeCrc32c = crc32c(recipe.data(), recipe.size());
  header.segmentId = segmentId;
  header.sourceOffset = commit.sourceOffset;
  return header;
}

inline void validateNativeLeafHeaderBeforeBody(
    const RoutedSegmentHeader& header, uint64_t totalDecodedLength,
    uint64_t expectedSourceOffset, const ExpertLimits& limits) {
  if (header.kind != SegmentKind::ROUTED_PROFILE ||
      header.transform != TransformId::NONE || header.recipeLength != 0 ||
      hasSegmentFlag(header.flags, SegmentFlag::RAW_ESCAPE))
    quit("Native routed leaf uses an unsupported outer contract.");
  if (header.sourceOffset != expectedSourceOffset ||
      header.sourceOffset > totalDecodedLength ||
      header.decodedLength > totalDecodedLength - header.sourceOffset)
    quit("Routed segments contain a source gap, overlap or invalid range.");
  if (totalDecodedLength != 0 && header.decodedLength == 0)
    quit("A non-empty native routed archive contains an empty leaf.");

  if (header.expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1) {
    if (header.decoderContractVersion != 1 ||
        header.statePolicy != StatePolicy::CONTINUE_GROUP ||
        header.stateId != kPaqStateId ||
        header.decodedLength > kMaximumPaqFragmentDecodedBytes ||
        header.payloadLength == 0 ||
        header.payloadLength > kMaximumPaqFragmentPayloadBytes ||
        header.parameterLength != kPaqConfigV1CanonicalSize)
      quit("Native PAQ fragment header violates the frozen v1 contract.");
    return;
  }

  if (header.expert == ExpertId::OPENZL_FROZEN_V1) {
    uint64_t payloadBound = 0;
    if (!OpenZlPlanRegistry::decoderAvailable())
      quit("This build cannot decode the frozen OpenZL expert contract.");
    if (header.decoderContractVersion != kOpenZlDecoderContractVersion ||
        header.statePolicy != StatePolicy::RESET ||
        header.flags != (segmentFlag(SegmentFlag::START) |
                         segmentFlag(SegmentFlag::END)) ||
        header.decodedLength != kSaoSilesiaObjectBytes ||
        header.decodedLength > limits.maximumDecodedBytes ||
        header.parameterLength == 0 || header.parameterLength > 256 ||
        !openzl_expert_detail::checkedPayloadBound(
          header.decodedLength, payloadBound) ||
        header.payloadLength > payloadBound ||
        header.payloadLength > limits.maximumPayloadBytes)
      quit("Native OpenZL leaf header violates the frozen SAO contract.");
    return;
  }

  quit("Native routed archive names an unsupported leaf expert.");
}

inline void validatePaqEpochFlags(const RoutedSegmentHeader& header,
                                  bool& paqStarted, bool& paqEnded) {
  const bool starts = hasSegmentFlag(header.flags, SegmentFlag::START);
  const bool continues = hasSegmentFlag(header.flags, SegmentFlag::CONTINUE);
  const bool ends = hasSegmentFlag(header.flags, SegmentFlag::END);
  const uint16_t allowed = segmentFlag(SegmentFlag::START) |
                           segmentFlag(SegmentFlag::CONTINUE) |
                           segmentFlag(SegmentFlag::END);
  if ((header.flags & ~allowed) != 0 || paqEnded ||
      (!paqStarted && (!starts || continues)) ||
      (paqStarted && (starts || !continues)))
    quit("Native archive does not contain one canonical PAQ state epoch.");
  paqStarted = true;
  if (ends)
    paqEnded = true;
}

inline void validateNativeLeafParametersBeforePayload(
    const RoutedSegmentHeader& header,
    const std::vector<uint8_t>& parameters) {
  if (header.expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1) {
    PaqConfigV1 config;
    if (!decodePaqConfigCanonical(parameters, config))
      quit("Routed PAQ fragment has an invalid canonical configuration.");
    return;
  }
  if (header.expert == ExpertId::OPENZL_FROZEN_V1) {
    OpenZlFrozenParamsV1 params;
    if (!decodeOpenZlFrozenParams(
          parameters, header.decodedLength, params))
      quit("Routed OpenZL fragment has invalid frozen-plan parameters.");
    return;
  }
  quit("Native routed archive names an unsupported parameter contract.");
}

inline uint64_t compareDecoded(File* decoded, File* expected,
                               uint64_t sourceOffset, uint64_t length) {
  std::array<uint8_t, kRoutedCopyBufferBytes> decodedBytes{};
  std::array<uint8_t, kRoutedCopyBufferBytes> expectedBytes{};
  uint64_t compared = 0;
  while (compared != length) {
    const uint64_t request = std::min<uint64_t>(
      length - compared, decodedBytes.size());
    if (decoded->blockRead(decodedBytes.data(), request) != request)
      quit("Validated routed decoder output became truncated.");
    const uint64_t actual = expected->blockRead(expectedBytes.data(), request);
    const uint64_t common = std::min(actual, request);
    for (uint64_t index = 0; index < common; ++index) {
      if (decodedBytes[static_cast<size_t>(index)] !=
          expectedBytes[static_cast<size_t>(index)])
        return sourceOffset + compared + index + 1;
    }
    if (actual != request)
      return sourceOffset + compared + actual + 1;
    compared += request;
  }
  return 0;
}

} // namespace routed_execution_detail

inline bool nativeRoutedEncodingAllowed(const Shared& shared,
                                        uint8_t compressionLevel,
                                        bool multipleFileMode,
                                        bool hasExternalModelState) {
  const FrozenRoutingFeatures& features = nativeRoutedFeatures();
  return features.nativeSingleFile && !multipleFileMode &&
         !hasExternalModelState && compressionLevel != 0 &&
         !shared.GetOptionTrainExe() && !shared.GetOptionTrainTxt() &&
         !shared.GetOptionUseLSTM();
}

inline NativeRoutedStats encodeNativeSingleFile(
    File* archive, const char* sourceFilename, uint64_t sourceLength,
    Shared& shared, uint8_t compressionLevel,
    bool hasExternalModelState = false) {
  using namespace routed_execution_detail;
  if (sourceFilename == nullptr)
    quit("Invalid native routed encoder input.");
  routed_archive_detail::requireInitiallyEmptyOutput(archive);
  if (!nativeRoutedEncodingAllowed(
        shared, compressionLevel, shared.GetOptionMultipleFileMode(),
        hasExternalModelState))
    quit("Current options must use the complete legacy PAQ archive path.");

  // The routed-v1 PAQ decoder contract is scalar and archive-contained.
  // Initialize here so callers cannot accidentally derive config from the
  // command-line level variable while Shared still contains its default 0.
  shared.chosenSimd = SIMDType::SIMD_NONE;
  shared.init(compressionLevel);

  const PaqConfigV1 paqConfig = paqConfigFromShared(shared);
  const std::vector<uint8_t> canonicalPaqConfig =
    encodePaqConfigCanonical(paqConfig);
  const StateDescriptor paqState = makePaqStateDescriptor(
    kPaqStateId, {ProfileId::PAQ_DEFAULT, 1, 0}, canonicalPaqConfig);

  FileDisk source;
  source.open(sourceFilename, true);
  if (fileLengthPreservingPosition(&source) != sourceLength)
    quit("Native routed source length changed before planning.");
  SourcePlanner planner(&source, sourceLength, &shared, nativeRoutedFeatures());
  PreparedPlan prepared = planner.take();
  validatePreparedPlan(prepared, sourceLength);

  size_t firstPaq = prepared.commits.size();
  size_t lastPaq = prepared.commits.size();
  for (size_t index = 0; index < prepared.commits.size(); ++index) {
    if (prepared.commits.at(index).decision.expert ==
        ExpertId::PAQ_BLOCK_FRAGMENT_V1) {
      if (firstPaq == prepared.commits.size())
        firstPaq = index;
      lastPaq = index;
    }
  }

  RoutedArchiveHeader archiveHeader;
  archiveHeader.segmentCount = static_cast<uint32_t>(prepared.commits.size());
  archiveHeader.profileRegistryVersion = kProfileRegistryVersion;
  archiveHeader.encoderRuleSetId = kNativeRoutedRuleSetId;
  archiveHeader.totalDecodedLength = sourceLength;
  RoutedArchiveWriter writer(archive, archiveHeader);

  std::unique_ptr<PaqModelSession> paqSession;
  TransformOptions transformOptions(&shared);
  NativeRoutedStats stats;
  stats.sourceBytes = sourceLength;
  stats.segmentCount = archiveHeader.segmentCount;

  for (size_t index = 0; index < prepared.commits.size(); ++index) {
    const CommitUnit& commit = prepared.commits.at(index);
    std::unique_ptr<FileTmp> ownedPayload;
    File* payload = nullptr;
    const std::vector<uint8_t>* parameters = nullptr;
    std::vector<uint8_t> externalParameters;
    uint16_t flags = segmentFlag(SegmentFlag::START) |
                     segmentFlag(SegmentFlag::END);
    uint64_t compatibility = 0;

    if (commit.decision.expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1) {
      if (!paqSession)
        paqSession.reset(new PaqModelSession(&shared, paqConfig));
      ownedPayload.reset(new FileTmp());
      BoundedWriteFile boundedPayload(
        ownedPayload.get(), kMaximumPaqFragmentPayloadBytes);
      PaqModelSession::Fragment fragment =
        paqSession->beginFragment(COMPRESS, &boundedPayload);
      String blockLabel;
      encodeBlockPlanRange(&source, *prepared.blocks,
                           commit.firstBlockIndex, commit.blockCount,
                           fragment.coder(), blockLabel, 0.0f, 1.0f,
                           &transformOptions, 0, 0, true);
      fragment.finish();
      if (boundedPayload.highWater() >
          kMaximumPaqFragmentPayloadBytes)
        quit("Native PAQ fragment payload exceeds the decoder's 31-bit limit.");
      payload = ownedPayload.get();
      parameters = &canonicalPaqConfig;
      flags = paqSegmentFlags(index, firstPaq, lastPaq);
      compatibility = paqState.compatibilityHash;
      ++stats.paqFragmentCount;
    }
    else {
      payload = prepared.preparedExternalPayloads[index].get();
      if (payload == nullptr)
        quit("Final external route has no prepared payload.");
      externalParameters = commit.decision.parameters;
      parameters = &externalParameters;
      ++stats.externalExpertCount;
    }

    const RangeIdentity decoded = inspectRange(
      &source, commit.sourceOffset, commit.sourceLength);
    const RangeIdentity encoded = inspectRange(payload, 0, [&] {
      const uint64_t saved = payload->curPos();
      payload->setEnd();
      const uint64_t size = payload->curPos();
      payload->setpos(saved);
      return size;
    }());
    const RoutedSegmentHeader header = makeSegmentHeader(
      commit, index, decoded, encoded, *parameters,
      commit.reconstructionRecipe, flags, compatibility);
    RoutedSegmentSource segment;
    segment.header = header;
    segment.parameters = parameters;
    segment.payload = payload;
    segment.recipe = &commit.reconstructionRecipe;
    writer.writeNext(segment);
  }
  if (fileLengthPreservingPosition(&source) != sourceLength)
    quit("Native routed source length changed during encoding.");
  writer.finish();
  stats.archiveBytes = archive->curPos();
  source.close();
  return stats;
}

inline NativeRoutedStats decodeNativeSingleFile(
    File* archive, File* output, FMode outputMode, Shared& shared) {
  using namespace routed_execution_detail;
  if (archive == nullptr || output == nullptr ||
      (outputMode != FMode::FDECOMPRESS && outputMode != FMode::FCOMPARE))
    quit("Invalid native routed decoder input.");
  archive->setpos(0);
  RoutedArchiveCursor cursor(archive);
  if (cursor.header().profileRegistryVersion != kProfileRegistryVersion ||
      cursor.header().featureVersion != kFeatureVersion ||
      cursor.header().parserRegistryVersion != kParserRegistryVersion ||
      cursor.header().unicodeTableVersion != kUnicodeTableVersion)
    quit("Native routed archive uses an unsupported frozen registry set.");
  if (cursor.header().encoderRuleSetId != kNativeRoutedRuleSetId)
    quit("Routed archive uses an unsupported native encoder rule set.");
  if (cursor.header().totalDecodedLength == 0 &&
      cursor.header().segmentCount != 1)
    quit("An empty native routed archive must contain one canonical segment.");

  OpenZlFrozenExpert openZl;
  ExpertRegistry experts;
  experts.add(&openZl);
  ExpertLimits limits;
  std::unique_ptr<PaqModelSession> paqSession;
  std::vector<uint8_t> canonicalPaqConfig;
  NativeRoutedStats stats;
  stats.sourceBytes = cursor.header().totalDecodedLength;
  stats.segmentCount = cursor.header().segmentCount;
  uint64_t expectedSourceOffset = 0;
  bool paqStarted = false;
  bool paqEnded = false;

  for (uint32_t index = 0; index < cursor.header().segmentCount; ++index) {
    std::vector<uint8_t> parameters;
    std::vector<uint8_t> recipe;
    FileTmp payload;
    const RoutedSegmentHeader header =
      cursor.readNextHeader();
    validateNativeLeafHeaderBeforeBody(
      header, cursor.header().totalDecodedLength, expectedSourceOffset, limits);
    if (header.expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1)
      validatePaqEpochFlags(header, paqStarted, paqEnded);
    cursor.readPendingParameters(parameters);
    validateNativeLeafParametersBeforePayload(header, parameters);
    cursor.readPendingPayloadAndRecipe(&payload, recipe);
    if (!recipe.empty())
      quit("Native routed leaf unexpectedly contains a reconstruction recipe.");
    FileTmp decoded;
    BoundedWriteFile boundedDecoded(&decoded, header.decodedLength);

    if (header.expert == ExpertId::PAQ_BLOCK_FRAGMENT_V1) {
      PaqConfigV1 config;
      if (!decodePaqConfigCanonical(parameters, config))
        quit("Routed PAQ fragment has an invalid canonical configuration.");
      if (!paqSession) {
        canonicalPaqConfig = parameters;
        applyPaqConfig(config, shared);
        paqSession.reset(new PaqModelSession(&shared, config));
      }
      else if (!canonicalPaqConfigEquals(canonicalPaqConfig, parameters)) {
        quit("Routed PAQ fragments changed decoder configuration mid-state.");
      }
      PaqModelSession::Fragment fragment =
        paqSession->beginFragment(DECOMPRESS, &payload);
      TransformOptions transformOptions(&shared);
      decompressRecursive(&boundedDecoded, header.decodedLength,
                          fragment.coder(),
                          FMode::FDECOMPRESS, &transformOptions, 0, 0, true);
      fragment.finish();
      ++stats.paqFragmentCount;
    }
    else {
      const ExpertCodec* expert = experts.find(header.expert,
        header.expert == ExpertId::OPENZL_FROZEN_V1
          ? kOpenZlFrozenExpertRevision : 0);
      if (expert == nullptr ||
          expert->contract().decoderContractVersion !=
            header.decoderContractVersion)
        quit("Routed segment requires an unavailable expert decoder.");
      const ExpertDecodeStatus status = expert->decode(
        &payload, header.payloadLength, parameters, header.decodedLength,
        limits, &boundedDecoded);
      if (status != ExpertDecodeStatus::OK)
        quit("Routed expert rejected its archived payload contract.");
      ++stats.externalExpertCount;
    }

    boundedDecoded.requireComplete();
    validateDecodedSegment(header, &decoded);
    decoded.setpos(0);
    if (outputMode == FMode::FDECOMPRESS) {
      copyExact(&decoded, output, header.decodedLength,
                "Validated routed output became truncated.");
    }
    else if (stats.firstDifference == 0) {
      stats.firstDifference = compareDecoded(
        &decoded, output, header.sourceOffset, header.decodedLength);
    }
    expectedSourceOffset += header.decodedLength;
  }
  if (expectedSourceOffset != cursor.header().totalDecodedLength)
    quit("Routed segment coverage does not match the archive output length.");
  if (paqStarted != paqEnded)
    quit("Native routed archive ended with an incomplete PAQ state epoch.");
  if (outputMode == FMode::FCOMPARE && stats.firstDifference == 0 &&
      output->getchar() != EOF)
    stats.comparedFileIsLonger = true;
  stats.archiveBytes = archive->curPos();
  return stats;
}

} // namespace routed
