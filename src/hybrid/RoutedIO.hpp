#pragma once

#include "../Utils.hpp"
#include "../file/File.hpp"
#include "RoutedFormat.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace routed {

constexpr size_t kRoutedCopyBufferBytes = 64u * 1024u;

struct RangeIdentity {
  uint64_t length = 0;
  uint32_t crc32c = 0;
};

// A seekable read-only view whose position is relative to one exact source
// range. It is used for expert inputs so an implementation cannot consume the
// following commit unit.
class FileRangeView final : public File {
public:
  FileRangeView(File* source, uint64_t sourceOffset, uint64_t sourceLength)
    : source_(source), sourceOffset_(sourceOffset), sourceLength_(sourceLength) {
    if (source_ == nullptr ||
        sourceOffset_ > std::numeric_limits<uint64_t>::max() - sourceLength_)
      quit("Invalid routed source range.");
    source_->setpos(sourceOffset_);
  }

  bool open(const char*, bool) override {
    quit("Cannot open a file through a routed source-range view.");
    return false;
  }
  void create(const char*) override {
    quit("Cannot create a file through a routed source-range view.");
  }
  void close() override {}

  int getchar() override {
    if (position_ == sourceLength_)
      return EOF;
    const int value = source_->getchar();
    if (value != EOF)
      ++position_;
    return value;
  }

  void putChar(uint8_t) override {
    quit("Cannot write through a routed source-range view.");
  }

  uint64_t blockRead(uint8_t* destination, uint64_t count) override {
    const uint64_t remaining = sourceLength_ - position_;
    if (count > remaining)
      count = remaining;
    const uint64_t bytesRead = source_->blockRead(destination, count);
    position_ += bytesRead;
    return bytesRead;
  }

  void blockWrite(uint8_t*, uint64_t) override {
    quit("Cannot write through a routed source-range view.");
  }

  void setpos(uint64_t newPosition) override {
    if (newPosition > sourceLength_)
      quit("Routed source-range seek is outside the declared range.");
    source_->setpos(sourceOffset_ + newPosition);
    position_ = newPosition;
  }

  void setEnd() override { setpos(sourceLength_); }
  uint64_t curPos() override { return position_; }
  bool eof() override { return position_ == sourceLength_; }
  uint64_t length() const { return sourceLength_; }
  uint64_t remaining() const { return sourceLength_ - position_; }

private:
  File* source_ = nullptr;
  uint64_t sourceOffset_ = 0;
  uint64_t sourceLength_ = 0;
  uint64_t position_ = 0;
};

// A seekable output view that refuses to create more than maximumLength bytes.
// It is intended for decoders whose archived decodedLength is a hard resource
// and integrity boundary.  The wrapped file must be positioned at its end, so
// the view cannot silently overwrite an existing suffix.  Positions exposed by
// this class are relative to that initial end position.
class BoundedWriteFile final : public File {
public:
  BoundedWriteFile(File* destination, uint64_t maximumLength)
      : destination_(destination), maximumLength_(maximumLength) {
    if (destination_ == nullptr)
      quit("Bounded routed output is unavailable.");
    baseOffset_ = destination_->curPos();
    if (baseOffset_ > std::numeric_limits<uint64_t>::max() - maximumLength_)
      quit("Bounded routed output range overflows.");
    destination_->setEnd();
    const uint64_t end = destination_->curPos();
    destination_->setpos(baseOffset_);
    if (end != baseOffset_)
      quit("Bounded routed output must begin at the destination end.");
  }

  BoundedWriteFile(const BoundedWriteFile&) = delete;
  BoundedWriteFile& operator=(const BoundedWriteFile&) = delete;

  bool open(const char*, bool) override {
    quit("Cannot open a file through a bounded routed output view.");
    return false;
  }
  void create(const char*) override {
    quit("Cannot create a file through a bounded routed output view.");
  }
  void close() override {}

  int getchar() override {
    if (position_ >= extent_)
      return EOF;
    syncDestinationPosition();
    const int value = destination_->getchar();
    if (value == EOF)
      quit("Bounded routed output became truncated while reading.");
    ++position_;
    return value;
  }

  void putChar(uint8_t value) override {
    requireWriteCapacity(1);
    syncDestinationPosition();
    destination_->putChar(value);
    ++position_;
    if (extent_ < position_)
      extent_ = position_;
  }

  uint64_t blockRead(uint8_t* destination, uint64_t count) override {
    if (count > extent_ - position_)
      count = extent_ - position_;
    if (count == 0)
      return 0;
    syncDestinationPosition();
    const uint64_t bytesRead = destination_->blockRead(destination, count);
    if (bytesRead > count)
      quit("Bounded routed output returned an invalid read length.");
    position_ += bytesRead;
    return bytesRead;
  }

  void blockWrite(uint8_t* source, uint64_t count) override {
    requireWriteCapacity(count);
    if (count == 0)
      return;
    syncDestinationPosition();
    destination_->blockWrite(source, count);
    position_ += count;
    if (extent_ < position_)
      extent_ = position_;
  }

  void setpos(uint64_t newPosition) override {
    if (newPosition > extent_)
      quit("Bounded routed output cannot seek into unwritten space.");
    position_ = newPosition;
    syncDestinationPosition();
  }

  void setEnd() override {
    position_ = extent_;
    syncDestinationPosition();
  }

  uint64_t curPos() override { return position_; }
  bool eof() override { return position_ >= extent_; }

  uint64_t maximumLength() const { return maximumLength_; }
  uint64_t highWater() const { return extent_; }
  uint64_t writtenLength() const { return extent_; }
  uint64_t remainingCapacity() const { return maximumLength_ - position_; }
  bool complete() const { return extent_ == maximumLength_; }

  void requireComplete() const {
    if (!complete())
      quit("Bounded routed decoder produced fewer bytes than declared.");
  }

private:
  void requireWriteCapacity(uint64_t count) const {
    if (position_ > maximumLength_ || count > maximumLength_ - position_)
      quit("Routed decoder exceeded the segment decoded-length boundary.");
  }

  void syncDestinationPosition() {
    destination_->setpos(baseOffset_ + position_);
  }

  File* destination_ = nullptr;
  uint64_t baseOffset_ = 0;
  uint64_t maximumLength_ = 0;
  uint64_t position_ = 0;
  uint64_t extent_ = 0;
};

inline RangeIdentity inspectRange(File* source, uint64_t sourceOffset,
                                  uint64_t sourceLength) {
  if (source == nullptr)
    quit("Routed range input is unavailable.");
  const uint64_t savedPosition = source->curPos();
  FileRangeView range(source, sourceOffset, sourceLength);
  std::array<uint8_t, kRoutedCopyBufferBytes> buffer{};
  Crc32c crc;
  uint64_t length = 0;
  while (length != sourceLength) {
    const uint64_t request = sourceLength - length < buffer.size()
      ? sourceLength - length : buffer.size();
    const uint64_t bytesRead = range.blockRead(buffer.data(), request);
    if (bytesRead != request)
      quit("Routed source range is truncated.");
    crc.update(buffer.data(), static_cast<size_t>(bytesRead));
    length += bytesRead;
  }
  source->setpos(savedPosition);
  return {length, crc.value()};
}

inline uint32_t copyExact(File* source, File* destination, uint64_t length,
                          const char* truncatedMessage) {
  if (source == nullptr || destination == nullptr)
    quit("Routed copy input or output is unavailable.");
  std::array<uint8_t, kRoutedCopyBufferBytes> buffer{};
  Crc32c crc;
  uint64_t remaining = length;
  while (remaining != 0) {
    const uint64_t request = remaining < buffer.size()
      ? remaining : buffer.size();
    const uint64_t bytesRead = source->blockRead(buffer.data(), request);
    if (bytesRead != request)
      quit(truncatedMessage);
    crc.update(buffer.data(), static_cast<size_t>(bytesRead));
    destination->blockWrite(buffer.data(), bytesRead);
    remaining -= bytesRead;
  }
  return crc.value();
}

inline bool compareExact(File* left, File* right, uint64_t length) {
  if (left == nullptr || right == nullptr)
    return false;
  const uint64_t leftPosition = left->curPos();
  const uint64_t rightPosition = right->curPos();
  std::array<uint8_t, kRoutedCopyBufferBytes> leftBuffer{};
  std::array<uint8_t, kRoutedCopyBufferBytes> rightBuffer{};
  uint64_t remaining = length;
  bool equal = true;
  while (remaining != 0 && equal) {
    const uint64_t request = remaining < leftBuffer.size()
      ? remaining : leftBuffer.size();
    if (left->blockRead(leftBuffer.data(), request) != request ||
        right->blockRead(rightBuffer.data(), request) != request) {
      equal = false;
      break;
    }
    for (uint64_t i = 0; i < request; ++i) {
      if (leftBuffer[static_cast<size_t>(i)] !=
          rightBuffer[static_cast<size_t>(i)]) {
        equal = false;
        break;
      }
    }
    remaining -= request;
  }
  left->setpos(leftPosition);
  right->setpos(rightPosition);
  return equal;
}

} // namespace routed
