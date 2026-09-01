#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace routed {

// ContainerLayout is encoder-side source geometry.  It never contains copied
// bytes or a reconstruction program: every span refers to one exact interval
// in the original input, and the sealed span list covers that input interval
// once, in order.  Consequently reconstruction remains ordinary decoded
// segment concatenation.
enum class ContainerKind : uint8_t {
  ZIP = 1
};

enum class ContainerSpanKind : uint8_t {
  // Headers, extra fields, names, comments, directory records, gaps and
  // trailing bytes.  These bytes remain on the PAQ path.
  STRUCTURE = 0,

  // Exact method-0, unencrypted member data whose size and CRC were verified.
  // Only this span kind may be recursively planned by the routed encoder.
  STORED_MEMBER_DATA = 1,

  // A complete local-file record which is not eligible for stored-data
  // exposure.  It includes the local header, encoded data and descriptor and
  // remains opaque PAQ input.
  OPAQUE_MEMBER = 2
};

constexpr uint32_t kNoContainerMember =
  std::numeric_limits<uint32_t>::max();

struct ContainerSpan {
  uint64_t sourceOffset = 0;
  uint64_t sourceLength = 0;
  ContainerSpanKind kind = ContainerSpanKind::STRUCTURE;
  uint32_t memberIndex = kNoContainerMember;
  uint16_t method = 0;
  uint16_t generalPurposeFlags = 0;
  uint32_t expectedCrc32 = 0;

  bool recursivelyPlannable() const {
    return kind == ContainerSpanKind::STORED_MEMBER_DATA;
  }
};

class ContainerLayout {
public:
  void clear() {
    kind_ = ContainerKind::ZIP;
    sourceOffset_ = 0;
    sourceLength_ = 0;
    nextOffset_ = 0;
    begun_ = false;
    sealed_ = false;
    spans_.clear();
  }

  bool begin(ContainerKind kind, uint64_t sourceOffset,
             uint64_t sourceLength) {
    clear();
    if (sourceOffset > std::numeric_limits<uint64_t>::max() - sourceLength)
      return false;
    kind_ = kind;
    sourceOffset_ = sourceOffset;
    sourceLength_ = sourceLength;
    nextOffset_ = sourceOffset;
    begun_ = true;
    return true;
  }

  // Spans must be appended in source order.  Zero-length regions are omitted;
  // this keeps empty stored members as structure-only local records.
  bool append(const ContainerSpan& span) {
    if (!begun_ || sealed_ || span.sourceLength == 0 ||
        span.sourceOffset != nextOffset_ ||
        span.sourceOffset > std::numeric_limits<uint64_t>::max() -
          span.sourceLength)
      return false;
    const uint64_t end = span.sourceOffset + span.sourceLength;
    const uint64_t layoutEnd = sourceOffset_ + sourceLength_;
    if (end > layoutEnd)
      return false;
    if (span.kind == ContainerSpanKind::STRUCTURE &&
        span.memberIndex != kNoContainerMember)
      return false;
    if (span.kind != ContainerSpanKind::STRUCTURE &&
        span.memberIndex == kNoContainerMember)
      return false;

    // Only generic structure spans are merged.  Member spans retain their
    // identity even if two adjacent entries happen to use the same method.
    if (!spans_.empty() &&
        span.kind == ContainerSpanKind::STRUCTURE &&
        spans_.back().kind == ContainerSpanKind::STRUCTURE &&
        spans_.back().sourceOffset + spans_.back().sourceLength ==
          span.sourceOffset) {
      spans_.back().sourceLength += span.sourceLength;
    } else {
      spans_.push_back(span);
    }
    nextOffset_ = end;
    return true;
  }

  bool appendStructure(uint64_t sourceOffset, uint64_t sourceLength) {
    if (sourceLength == 0)
      return begun_ && !sealed_ && sourceOffset == nextOffset_;
    ContainerSpan span;
    span.sourceOffset = sourceOffset;
    span.sourceLength = sourceLength;
    span.kind = ContainerSpanKind::STRUCTURE;
    return append(span);
  }

  bool seal() {
    if (!begun_ || sealed_ ||
        nextOffset_ != sourceOffset_ + sourceLength_)
      return false;
    sealed_ = true;
    return true;
  }

  ContainerKind kind() const { return kind_; }
  uint64_t sourceOffset() const { return sourceOffset_; }
  uint64_t sourceLength() const { return sourceLength_; }
  bool sealed() const { return sealed_; }
  const std::vector<ContainerSpan>& spans() const { return spans_; }

  bool validCoverage() const {
    if (!begun_ || !sealed_)
      return false;
    uint64_t expected = sourceOffset_;
    for (const ContainerSpan& span : spans_) {
      if (span.sourceLength == 0 || span.sourceOffset != expected ||
          span.sourceOffset > std::numeric_limits<uint64_t>::max() -
            span.sourceLength)
        return false;
      expected += span.sourceLength;
    }
    return expected == sourceOffset_ + sourceLength_;
  }

private:
  ContainerKind kind_ = ContainerKind::ZIP;
  uint64_t sourceOffset_ = 0;
  uint64_t sourceLength_ = 0;
  uint64_t nextOffset_ = 0;
  bool begun_ = false;
  bool sealed_ = false;
  std::vector<ContainerSpan> spans_;
};

} // namespace routed
