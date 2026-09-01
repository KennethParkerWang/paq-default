#pragma once

#include "../Utils.hpp"
#include "../file/File.hpp"
#include "FrozenRuleSet.hpp"
#include "RoutedFormat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace routed {

namespace routed_archive_detail {

constexpr size_t kCopyBufferSize = 64u * 1024u;

struct IdentityMetadata {
  uint64_t length = 0;
  uint32_t crc32c = 0;
};

// File::curPos() alone cannot distinguish an empty output from a non-empty
// file that has merely been rewound.  Reject the latter before any routed
// header is committed, otherwise a stale suffix would make the writer's own
// output undecodable as a canonical archive.
inline void requireInitiallyEmptyOutput(File* output) {
  if (output == nullptr || output->curPos() != 0)
    quit("Routed archive output must start empty.");
  output->setEnd();
  const bool empty = output->curPos() == 0;
  output->setpos(0);
  if (!empty)
    quit("Routed archive output must not contain existing bytes.");
}

inline IdentityMetadata inspectIdentity(File* input) {
  if (input == nullptr)
    quit("Routed archive input is unavailable.");

  const uint64_t savedPosition = input->curPos();
  input->setpos(0);
  std::array<uint8_t, kCopyBufferSize> buffer{};
  Crc32c crc;
  uint64_t length = 0;
  while (true) {
    const uint64_t bytesRead = input->blockRead(buffer.data(), buffer.size());
    if (bytesRead == 0)
      break;
    if (length > std::numeric_limits<uint64_t>::max() - bytesRead)
      quit("Routed archive input length overflow.");
    crc.update(buffer.data(), static_cast<size_t>(bytesRead));
    length += bytesRead;
  }
  input->setpos(savedPosition);

  IdentityMetadata result;
  result.length = length;
  result.crc32c = crc.value();
  return result;
}

inline uint32_t copyExact(File* input, File* output, uint64_t length,
                          const char* truncatedMessage) {
  if (input == nullptr || output == nullptr)
    quit("Routed archive input or output is unavailable.");

  std::array<uint8_t, kCopyBufferSize> buffer{};
  Crc32c crc;
  uint64_t remaining = length;
  while (remaining != 0) {
    const uint64_t request = remaining < buffer.size() ? remaining : buffer.size();
    const uint64_t bytesRead = input->blockRead(buffer.data(), request);
    if (bytesRead != request)
      quit(truncatedMessage);
    crc.update(buffer.data(), static_cast<size_t>(bytesRead));
    output->blockWrite(buffer.data(), bytesRead);
    remaining -= bytesRead;
  }
  return crc.value();
}

inline StateDescriptor stateDescriptor(const RoutedSegmentHeader& segment) {
  StateDescriptor descriptor;
  descriptor.stateId = segment.stateId;
  descriptor.pipeline = segment.pipeline;
  descriptor.expert = segment.expert;
  descriptor.compatibilityHash = segment.stateCompatibility;
  descriptor.policy = segment.statePolicy;
  return descriptor;
}

inline StateDescriptor stateDescriptor(
    const RoutedSegmentHeader& segment,
    const std::vector<uint8_t>& canonicalParameters) {
  if (segment.expert != ExpertId::PAQ_BLOCK_FRAGMENT_V1)
    return stateDescriptor(segment);
  StateDescriptor descriptor = makePaqStateDescriptor(
    segment.stateId, segment.pipeline, canonicalParameters);
  if (segment.statePolicy != descriptor.policy ||
      segment.stateCompatibility != descriptor.compatibilityHash)
    quit("PAQ fragment header disagrees with its canonical state config.");
  return descriptor;
}

inline void requireEndOfArchive(File* archive) {
  uint8_t trailing = 0;
  if (archive->blockRead(&trailing, 1) != 0)
    quit("Routed archive contains trailing bytes.");
}

inline bool isSingleLegacySegmentContract(const RoutedSegmentHeader& segment) {
  return segment.kind == SegmentKind::LEGACY_PAQ_BLOCK &&
      segment.pipeline.profileId == ProfileId::PAQ_DEFAULT &&
      segment.pipeline.profileRevision == 1 &&
      segment.pipeline.variantId == 0 &&
      segment.evidence == EvidenceKind::EXACT_FORMAT &&
      segment.statePolicy == StatePolicy::RESET &&
      segment.expert == ExpertId::PAQ_LEGACY_ARCHIVE &&
      segment.transform == TransformId::NONE &&
      segment.flags == (segmentFlag(SegmentFlag::START) |
                        segmentFlag(SegmentFlag::END)) &&
      segment.decoderContractVersion == 1 &&
      segment.stateId == 0 && segment.stateCompatibility == 0 &&
      segment.parameterLength == 0 && segment.recipeLength == 0 &&
      segment.parameterCrc32c == 0 && segment.recipeCrc32c == 0 &&
      segment.decodedLength == segment.payloadLength &&
      segment.decodedCrc32c == segment.payloadCrc32c;
}

inline void requireLegacyEnvelope(const RoutedSegmentHeader& segment) {
  if (!isSingleLegacySegmentContract(segment))
    quit("Routed legacy PAQ segment has an incompatible decoder contract.");
}

/*
 * Kept private to this header: native/legacy dispatch must inspect the actual
 * first decoder contract, never infer it from segmentCount alone.
 */
inline bool isLegacyArchiveLayout(const RoutedArchiveHeader& archive,
                                  const RoutedSegmentHeader& segment) {
  return archive.segmentCount == 1 &&
      archive.totalDecodedLength == segment.decodedLength &&
      isSingleLegacySegmentContract(segment);
}

} // namespace routed_archive_detail

struct RoutedArchiveLayout {
  RoutedArchiveHeader archive;
  RoutedSegmentHeader firstSegment;
  bool legacySingleArchive = false;
};

// Reads only canonical metadata and restores the caller's position.  Dispatch
// is based on the immutable first decoder contract; native routed archives are
// allowed to contain either one or many segments.
inline RoutedArchiveLayout inspectRoutedArchiveLayout(File* archive) {
  if (archive == nullptr)
    quit("Routed archive input is unavailable.");
  const uint64_t savedPosition = archive->curPos();
  archive->setpos(0);
  RoutedArchiveLayout layout;
  layout.archive = readRoutedArchiveHeader(archive);
  layout.firstSegment = readRoutedSegmentHeader(archive);
  layout.legacySingleArchive = routed_archive_detail::isLegacyArchiveLayout(
    layout.archive, layout.firstSegment);
  archive->setpos(savedPosition);
  return layout;
}

struct RoutedSegmentSource {
  RoutedSegmentHeader header;
  const std::vector<uint8_t>* parameters = nullptr;
  File* payload = nullptr;
  const std::vector<uint8_t>* recipe = nullptr;
};

inline const std::vector<uint8_t>& routedBytesOrEmpty(
    const std::vector<uint8_t>* bytes) {
  static const std::vector<uint8_t> empty;
  return bytes == nullptr ? empty : *bytes;
}

inline routed_archive_detail::IdentityMetadata validateRoutedSegmentBody(
    const RoutedSegmentSource& source) {
  if (isRawStorageRoute(source.header.pipeline.profileId,
                        source.header.expert) ||
      hasSegmentFlag(source.header.flags, SegmentFlag::RAW_ESCAPE))
    quit("RAW storage routes have been removed from the encoder.");
  const std::vector<uint8_t>& parameters =
    routedBytesOrEmpty(source.parameters);
  const std::vector<uint8_t>& recipe = routedBytesOrEmpty(source.recipe);
  if (source.payload == nullptr)
    quit("Routed segment payload is unavailable.");
  const routed_archive_detail::IdentityMetadata payload =
    routed_archive_detail::inspectIdentity(source.payload);
  if (source.header.parameterLength != parameters.size() ||
      source.header.payloadLength != payload.length ||
      source.header.recipeLength != recipe.size() ||
      source.header.parameterCrc32c !=
        crc32c(parameters.data(), parameters.size()) ||
      source.header.payloadCrc32c != payload.crc32c ||
      source.header.recipeCrc32c != crc32c(recipe.data(), recipe.size()))
    quit("Routed segment body does not match its archived metadata.");
  return payload;
}

inline void writeValidatedRoutedSegmentBody(
    File* archive, const RoutedSegmentSource& source,
    const routed_archive_detail::IdentityMetadata& payload) {
  const std::vector<uint8_t>& parameters =
    routedBytesOrEmpty(source.parameters);
  const std::vector<uint8_t>& recipe = routedBytesOrEmpty(source.recipe);

  if (!parameters.empty())
    routedWriteExact(archive, const_cast<uint8_t*>(parameters.data()),
                     parameters.size());
  const uint64_t savedPosition = source.payload->curPos();
  source.payload->setpos(0);
  const uint32_t payloadCrc = routed_archive_detail::copyExact(
    source.payload, archive, payload.length,
    "Routed segment payload changed length while it was written.");
  source.payload->setpos(savedPosition);
  if (payloadCrc != payload.crc32c)
    quit("Routed segment payload changed while it was written.");
  if (!recipe.empty())
    routedWriteExact(archive, const_cast<uint8_t*>(recipe.data()), recipe.size());
}

inline void writeRoutedSegmentBody(File* archive,
                                   const RoutedSegmentSource& source) {
  const routed_archive_detail::IdentityMetadata payload =
    validateRoutedSegmentBody(source);
  writeValidatedRoutedSegmentBody(archive, source, payload);
}

inline void readRoutedSegmentParameters(
    File* archive, const RoutedSegmentHeader& header,
    std::vector<uint8_t>& parameters) {
  if (archive == nullptr)
    quit("Routed segment input is unavailable.");
  parameters.resize(static_cast<size_t>(header.parameterLength));
  if (!parameters.empty())
    routedReadExact(archive, parameters.data(), parameters.size(),
                    "Routed segment parameters are truncated.");
  if (crc32c(parameters.data(), parameters.size()) != header.parameterCrc32c)
    quit("Routed segment parameter checksum mismatch.");
}

inline void readRoutedSegmentPayloadAndRecipe(
    File* archive, const RoutedSegmentHeader& header, File* payload,
    std::vector<uint8_t>& recipe) {
  if (archive == nullptr || payload == nullptr)
    quit("Routed segment input or payload output is unavailable.");
  payload->setEnd();
  if (payload->curPos() != 0)
    quit("Routed segment payload output must initially be empty.");
  payload->setpos(0);

  const uint32_t payloadCrc = routed_archive_detail::copyExact(
    archive, payload, header.payloadLength,
    "Routed segment payload is truncated.");
  if (payloadCrc != header.payloadCrc32c)
    quit("Routed segment payload checksum mismatch.");

  recipe.resize(static_cast<size_t>(header.recipeLength));
  if (!recipe.empty())
    routedReadExact(archive, recipe.data(), recipe.size(),
                    "Routed segment reconstruction recipe is truncated.");
  if (crc32c(recipe.data(), recipe.size()) != header.recipeCrc32c)
    quit("Routed segment reconstruction recipe checksum mismatch.");
  payload->setEnd();
  if (payload->curPos() != header.payloadLength)
    quit("Routed segment payload output length mismatch.");
  payload->setpos(0);
}

inline void readRoutedSegmentBody(File* archive,
                                  const RoutedSegmentHeader& header,
                                  std::vector<uint8_t>& parameters,
                                  File* payload,
                                  std::vector<uint8_t>& recipe) {
  readRoutedSegmentParameters(archive, header, parameters);
  readRoutedSegmentPayloadAndRecipe(archive, header, payload, recipe);
}

inline void validateDecodedSegment(const RoutedSegmentHeader& header,
                                   File* decoded) {
  const routed_archive_detail::IdentityMetadata actual =
    routed_archive_detail::inspectIdentity(decoded);
  if (actual.length != header.decodedLength ||
      actual.crc32c != header.decodedCrc32c)
    quit("Routed expert output does not match the decoded segment contract.");
}

inline void writeRoutedArchive(File* archive,
                               const RoutedArchiveHeader& header,
                               const std::vector<RoutedSegmentSource>& segments) {
  if (segments.size() != header.segmentCount)
    quit("Routed archive segment table does not match its header.");
  routed_archive_detail::requireInitiallyEmptyOutput(archive);
  for (const RoutedSegmentSource& source : segments) {
    if (isRawStorageRoute(source.header.pipeline.profileId,
                          source.header.expert) ||
        hasSegmentFlag(source.header.flags, SegmentFlag::RAW_ESCAPE))
      quit("RAW storage routes have been removed from the encoder.");
  }
  writeRoutedArchiveHeader(archive, header);
  StateManager states;
  for (size_t index = 0; index < segments.size(); ++index) {
    const RoutedSegmentSource& source = segments[index];
    if (source.header.segmentId != index)
      quit("Routed archive segment identifiers are not canonical.");
    const std::vector<uint8_t>& parameters =
      routedBytesOrEmpty(source.parameters);
    const StateDescriptor descriptor =
      routed_archive_detail::stateDescriptor(source.header, parameters);
    states.beginSegment(source.header.flags, descriptor);
    writeRoutedSegmentHeader(archive, source.header);
    writeRoutedSegmentBody(archive, source);
    states.finishSegment(source.header.flags, descriptor);
  }
  states.requireAllClosed();
}

// Writes a routed archive incrementally while preserving the same on-wire
// segment layout as writeRoutedArchive(). Ordinary leaf segments are written
// in canonical sourceOffset order and, without a reconstruction root, cover
// the complete decoded source. A future CONTAINER_MAP is tracked separately,
// so its reconstructed root never double-counts source-aligned leaf bytes.
class RoutedArchiveWriter {
public:
  RoutedArchiveWriter(File* archive, const RoutedArchiveHeader& header)
      : archive_(archive), header_(header) {
    routed_archive_detail::requireInitiallyEmptyOutput(archive_);
    writeRoutedArchiveHeader(archive_, header_);
  }

  RoutedArchiveWriter(const RoutedArchiveWriter&) = delete;
  RoutedArchiveWriter& operator=(const RoutedArchiveWriter&) = delete;

  const RoutedArchiveHeader& header() const { return header_; }
  uint32_t segmentsWritten() const { return nextSegment_; }
  uint64_t leafDecodedLength() const { return leafDecodedLength_; }
  bool hasContainerRoot() const { return hasContainerRoot_; }
  uint64_t containerRootDecodedLength() const {
    return containerRootDecodedLength_;
  }
  bool finished() const { return finished_; }

  void writeNext(const RoutedSegmentSource& source) {
    if (finished_)
      quit("Cannot append a segment to a finished routed archive.");
    if (nextSegment_ >= header_.segmentCount)
      quit("Routed archive writer received too many segments.");
    if (source.header.segmentId != nextSegment_)
      quit("Routed archive segment identifiers are not canonical.");

    // Validate the complete body before committing its header. This prevents
    // a stale length or CRC from producing a partly committed segment.
    validateSegmentHeader(source.header);
    const routed_archive_detail::IdentityMetadata payload =
      validateRoutedSegmentBody(source);
    validateCoverageBeforeWrite(source.header);

    const std::vector<uint8_t>& parameters =
      routedBytesOrEmpty(source.parameters);
    const StateDescriptor descriptor =
      routed_archive_detail::stateDescriptor(source.header, parameters);
    states_.beginSegment(source.header.flags, descriptor);
    writeRoutedSegmentHeader(archive_, source.header);
    writeValidatedRoutedSegmentBody(archive_, source, payload);
    states_.finishSegment(source.header.flags, descriptor);

    commitCoverage(source.header);
    ++nextSegment_;
  }

  void finish() {
    if (finished_)
      quit("Routed archive writer was finished more than once.");
    if (nextSegment_ != header_.segmentCount)
      quit("Routed archive ended before its declared segment count.");
    states_.requireAllClosed();

    if (hasContainerRoot_) {
      if (containerRootDecodedLength_ != header_.totalDecodedLength)
        quit("Routed container root length does not match the archive header.");
    }
    else if (leafDecodedLength_ != header_.totalDecodedLength) {
      quit("Routed leaf coverage does not match the archive decoded length.");
    }
    finished_ = true;
  }

private:
  void validateCoverageBeforeWrite(const RoutedSegmentHeader& segment) const {
    if (segment.kind == SegmentKind::CONTAINER_MAP) {
      if (hasContainerRoot_)
        quit("Routed archive contains more than one container root.");
      if (nextSegment_ + 1 != header_.segmentCount)
        quit("Routed container root must be the final segment.");
      if (segment.sourceOffset != 0)
        quit("Routed container root must begin at source offset zero.");
      if (segment.decodedLength != header_.totalDecodedLength)
        quit("Routed container root length does not match the archive header.");
      return;
    }

    if (segment.sourceOffset != leafDecodedLength_)
      quit("Routed leaf segments contain a source gap or overlap.");
    if (segment.decodedLength == 0 && header_.totalDecodedLength != 0)
      quit("A non-empty routed archive contains an empty leaf segment.");
    if (leafDecodedLength_ > header_.totalDecodedLength ||
        segment.decodedLength >
        header_.totalDecodedLength - leafDecodedLength_)
      quit("Routed leaf coverage exceeds the archive decoded length.");
  }

  void commitCoverage(const RoutedSegmentHeader& segment) {
    if (segment.kind == SegmentKind::CONTAINER_MAP) {
      hasContainerRoot_ = true;
      containerRootDecodedLength_ = segment.decodedLength;
      return;
    }
    leafDecodedLength_ += segment.decodedLength;
  }

  File* archive_ = nullptr;
  RoutedArchiveHeader header_;
  StateManager states_;
  uint32_t nextSegment_ = 0;
  uint64_t leafDecodedLength_ = 0;
  uint64_t containerRootDecodedLength_ = 0;
  bool hasContainerRoot_ = false;
  bool finished_ = false;
};

class RoutedArchiveCursor {
public:
  explicit RoutedArchiveCursor(File* archive) : archive_(archive) {
    if (archive_ == nullptr)
      quit("Routed archive input is unavailable.");
    header_ = readRoutedArchiveHeader(archive_);
  }

  const RoutedArchiveHeader& header() const { return header_; }
  uint32_t remainingSegments() const {
    return header_.segmentCount - nextSegment_;
  }

  bool hasPendingBody() const { return hasPendingHeader_; }
  bool hasPendingParameters() const { return hasPendingParameters_; }

  const RoutedSegmentHeader& pendingHeader() const {
    if (!hasPendingHeader_)
      quit("Routed archive cursor has no pending segment header.");
    return pendingHeader_;
  }

  // Header and body are deliberately separate operations. Native decoders can
  // reject an unsupported decoder contract or an excessive payload length
  // before readRoutedSegmentBody() allocates or copies the declared body.
  RoutedSegmentHeader readNextHeader() {
    if (hasPendingHeader_)
      quit("Routed archive cursor must consume the pending segment body first.");
    if (nextSegment_ >= header_.segmentCount)
      quit("Routed archive cursor has no remaining segments.");
    pendingHeader_ = readRoutedSegmentHeader(archive_);
    if (pendingHeader_.segmentId != nextSegment_)
      quit("Routed archive segment identifiers are not canonical.");
    hasPendingHeader_ = true;
    return pendingHeader_;
  }

  void readPendingParameters(std::vector<uint8_t>& parameters) {
    if (!hasPendingHeader_ || hasPendingParameters_)
      quit("Routed archive cursor has no unread parameter section.");
    readRoutedSegmentParameters(archive_, pendingHeader_, parameters);
    pendingDescriptor_ =
      routed_archive_detail::stateDescriptor(pendingHeader_, parameters);
    states_.beginSegment(pendingHeader_.flags, pendingDescriptor_);
    hasPendingParameters_ = true;
  }

  void readPendingPayloadAndRecipe(File* payload,
                                   std::vector<uint8_t>& recipe) {
    if (!hasPendingHeader_ || !hasPendingParameters_)
      quit("Routed archive cursor must validate parameters before payload.");
    readRoutedSegmentPayloadAndRecipe(
      archive_, pendingHeader_, payload, recipe);
    states_.finishSegment(pendingHeader_.flags, pendingDescriptor_);

    hasPendingParameters_ = false;
    hasPendingHeader_ = false;
    ++nextSegment_;
    if (nextSegment_ == header_.segmentCount) {
      states_.requireAllClosed();
      routed_archive_detail::requireEndOfArchive(archive_);
    }
  }

  void readPendingBody(std::vector<uint8_t>& parameters,
                       File* payload,
                       std::vector<uint8_t>& recipe) {
    if (!hasPendingHeader_)
      quit("Routed archive cursor has no pending segment body.");
    readPendingParameters(parameters);
    readPendingPayloadAndRecipe(payload, recipe);
  }

  RoutedSegmentHeader readNext(std::vector<uint8_t>& parameters,
                               File* payload,
                               std::vector<uint8_t>& recipe) {
    const RoutedSegmentHeader segment = readNextHeader();
    readPendingBody(parameters, payload, recipe);
    return segment;
  }

private:
  File* archive_ = nullptr;
  RoutedArchiveHeader header_;
  StateManager states_;
  uint32_t nextSegment_ = 0;
  RoutedSegmentHeader pendingHeader_;
  StateDescriptor pendingDescriptor_;
  bool hasPendingHeader_ = false;
  bool hasPendingParameters_ = false;
};

// The first v2 writer deliberately wraps one complete PAQ archive as an
// indivisible decoder contract. This establishes the versioned segment table
// without changing the arithmetic stream; later routed experts can coexist in
// the same format without making v1/v2 detection ambiguous.
inline void writeSingleLegacyArchive(File* archive, File* legacyArchive) {
  routed_archive_detail::requireInitiallyEmptyOutput(archive);

  const routed_archive_detail::IdentityMetadata metadata =
    routed_archive_detail::inspectIdentity(legacyArchive);
  if (metadata.length > kMaximumRoutedFieldLength)
    quit("Routed legacy PAQ payload exceeds the archive resource limit.");

  RoutedArchiveHeader archiveHeader;
  archiveHeader.segmentCount = 1;
  archiveHeader.profileRegistryVersion = 1;
  archiveHeader.encoderRuleSetId = kProductionRuleSetId;
  archiveHeader.totalDecodedLength = metadata.length;

  RoutedSegmentHeader segment;
  segment.kind = SegmentKind::LEGACY_PAQ_BLOCK;
  segment.evidence = EvidenceKind::EXACT_FORMAT;
  segment.statePolicy = StatePolicy::RESET;
  segment.pipeline = {ProfileId::PAQ_DEFAULT, 1, 0};
  segment.expert = ExpertId::PAQ_LEGACY_ARCHIVE;
  segment.transform = TransformId::NONE;
  segment.flags = segmentFlag(SegmentFlag::START) |
                  segmentFlag(SegmentFlag::END);
  segment.decoderContractVersion = 1;
  segment.decodedLength = metadata.length;
  segment.payloadLength = metadata.length;
  segment.decodedCrc32c = metadata.crc32c;
  segment.payloadCrc32c = metadata.crc32c;

  writeRoutedArchiveHeader(archive, archiveHeader);
  writeRoutedSegmentHeader(archive, segment);

  const uint64_t savedPosition = legacyArchive->curPos();
  legacyArchive->setpos(0);
  const uint32_t actualCrc = routed_archive_detail::copyExact(
    legacyArchive, archive, metadata.length,
    "Legacy PAQ payload changed length while routed archive was written.");
  legacyArchive->setpos(savedPosition);
  if (actualCrc != metadata.crc32c)
    quit("Legacy PAQ payload changed while routed archive was written.");
}

inline RoutedSegmentHeader readSingleLegacyArchive(File* archive,
                                                   File* legacyOutput) {
  if (archive == nullptr || legacyOutput == nullptr)
    quit("Routed archive input or output is unavailable.");
  legacyOutput->setEnd();
  if (legacyOutput->curPos() != 0)
    quit("Routed legacy output must be empty before decoding.");
  legacyOutput->setpos(0);

  const RoutedArchiveHeader archiveHeader = readRoutedArchiveHeader(archive);
  if (archiveHeader.segmentCount != 1)
    quit("This decoder requires exactly one routed legacy PAQ segment.");

  const RoutedSegmentHeader segment = readRoutedSegmentHeader(archive);
  routed_archive_detail::requireLegacyEnvelope(segment);
  if (archiveHeader.totalDecodedLength != segment.decodedLength)
    quit("Routed archive decoded length does not match its segment table.");

  StateManager states;
  const StateDescriptor descriptor =
    routed_archive_detail::stateDescriptor(segment);
  states.beginSegment(segment.flags, descriptor);
  const uint32_t actualCrc = routed_archive_detail::copyExact(
    archive, legacyOutput, segment.payloadLength,
    "Routed legacy PAQ payload is truncated.");
  if (actualCrc != segment.payloadCrc32c)
    quit("Routed legacy PAQ payload checksum mismatch.");
  states.finishSegment(segment.flags, descriptor);
  states.requireAllClosed();
  routed_archive_detail::requireEndOfArchive(archive);

  legacyOutput->setEnd();
  if (legacyOutput->curPos() != segment.decodedLength)
    quit("Routed legacy PAQ decoded length mismatch.");
  legacyOutput->setpos(0);
  return segment;
}

} // namespace routed
