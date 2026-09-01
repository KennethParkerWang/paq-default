#pragma once

#include "HybridFormat.hpp"
#include "../file/FileTmp.hpp"

#include <array>
#include <cstdint>
#include <limits>

namespace hybrid {

struct BackendMetadata {
  uint64_t decodedLength = 0;
  uint64_t payloadLength = 0;
  uint32_t decodedCrc32 = 0;
  uint32_t payloadCrc32 = 0;
};

class Backend {
public:
  virtual ~Backend() = default;
  virtual RouteId route() const = 0;
  virtual DecoderContractId decoderContract() const = 0;
  virtual BackendMetadata inspect(File* decodedInput) const = 0;
  virtual void encode(File* decodedInput, File* payloadOutput,
                      const BackendMetadata& expected) const = 0;
  virtual void decode(File* payloadInput, File* decodedOutput,
                      const FrameHeader& frame) const = 0;
};

namespace backend_detail {

constexpr size_t kCopyBufferSize = 64u * 1024u;

// Presents exactly one frame payload as an independent read-only file. Even a
// backend that asks for more bytes receives EOF at the declared frame boundary,
// so it cannot consume a following frame header.
class BoundedInputFile final : public File {
public:
  BoundedInputFile(File* source, uint64_t length)
    : source_(source), basePosition_(source != nullptr ? source->curPos() : 0),
      length_(length), position_(0) {
    if (source_ == nullptr)
      quit("Hybrid bounded payload input is not available.");
    if (basePosition_ > std::numeric_limits<uint64_t>::max() - length_)
      quit("Hybrid bounded payload range overflow.");
  }

  bool open(const char*, bool) override {
    quit("Cannot open a file through a hybrid payload view.");
    return false;
  }
  void create(const char*) override {
    quit("Cannot create a file through a hybrid payload view.");
  }
  void close() override {}

  int getchar() override {
    if (position_ == length_)
      return EOF;
    const int value = source_->getchar();
    if (value != EOF)
      ++position_;
    return value;
  }

  void putChar(uint8_t) override {
    quit("Cannot write through a hybrid payload input view.");
  }

  uint64_t blockRead(uint8_t* destination, uint64_t count) override {
    const uint64_t remaining = length_ - position_;
    if (count > remaining)
      count = remaining;
    const uint64_t bytesRead = source_->blockRead(destination, count);
    position_ += bytesRead;
    return bytesRead;
  }

  void blockWrite(uint8_t*, uint64_t) override {
    quit("Cannot write through a hybrid payload input view.");
  }

  void setpos(uint64_t newPosition) override {
    if (newPosition > length_)
      quit("Hybrid backend attempted to seek outside its payload.");
    source_->setpos(basePosition_ + newPosition);
    position_ = newPosition;
  }

  void setEnd() override { setpos(length_); }
  uint64_t curPos() override { return position_; }
  bool eof() override { return position_ == length_; }
  uint64_t remaining() const { return length_ - position_; }

private:
  File* source_;
  uint64_t basePosition_;
  uint64_t length_;
  uint64_t position_;
};

inline BackendMetadata inspectIdentityPayload(File* input) {
  if (input == nullptr)
    quit("Hybrid backend input is not available.");

  const uint64_t savedPosition = input->curPos();
  input->setpos(0);
  std::array<uint8_t, kCopyBufferSize> buffer{};
  Crc32 crc;
  uint64_t length = 0;
  while (true) {
    const uint64_t bytesRead = input->blockRead(buffer.data(), buffer.size());
    if (bytesRead == 0)
      break;
    if (length > std::numeric_limits<uint64_t>::max() - bytesRead)
      quit("Hybrid backend input length overflow.");
    if (length > kMaximumFrameDataLength - bytesRead)
      quit("Hybrid backend input exceeds the v1 resource limit.");
    crc.update(buffer.data(), static_cast<size_t>(bytesRead));
    length += bytesRead;
  }
  input->setpos(savedPosition);

  BackendMetadata metadata;
  metadata.decodedLength = length;
  metadata.payloadLength = length;
  metadata.decodedCrc32 = crc.value();
  metadata.payloadCrc32 = crc.value();
  return metadata;
}

inline uint32_t copyExact(File* input, File* output, uint64_t length,
                          const char* truncatedMessage) {
  if (input == nullptr || output == nullptr)
    quit("Hybrid backend input or output is not available.");

  std::array<uint8_t, kCopyBufferSize> buffer{};
  Crc32 crc;
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

inline void encodeIdentityPayload(File* decodedInput, File* payloadOutput,
                                  const BackendMetadata& expected) {
  if (expected.decodedLength != expected.payloadLength ||
      expected.decodedCrc32 != expected.payloadCrc32)
    quit("Identity backend metadata is inconsistent.");

  const uint64_t savedPosition = decodedInput->curPos();
  decodedInput->setpos(0);
  const uint32_t actualCrc = copyExact(decodedInput, payloadOutput,
      expected.payloadLength, "Identity backend input was truncated.");
  decodedInput->setpos(savedPosition);
  if (actualCrc != expected.payloadCrc32)
    quit("Identity backend input changed while the frame was written.");
}

inline void decodeIdentityPayload(File* payloadInput, File* decodedOutput,
                                  const FrameHeader& frame) {
  if (payloadInput == nullptr || decodedOutput == nullptr)
    quit("Hybrid backend input or output is not available.");
  if (frame.recipeLength != 0 || frame.recipeCrc32 != 0)
    quit("Identity backend does not accept a reconstruction recipe.");
  if (frame.decodedLength != frame.payloadLength ||
      frame.decodedCrc32 != frame.payloadCrc32)
    quit("Identity backend frame lengths or checksums are inconsistent.");

  decodedOutput->setEnd();
  if (decodedOutput->curPos() != 0)
    quit("Hybrid backend output must be empty before decoding.");
  decodedOutput->setpos(0);

  const uint32_t actualCrc = copyExact(payloadInput, decodedOutput,
      frame.payloadLength, "Hybrid frame payload is truncated.");
  if (actualCrc != frame.payloadCrc32)
    quit("Hybrid frame payload checksum mismatch.");
  decodedOutput->setEnd();
  if (decodedOutput->curPos() != frame.decodedLength)
    quit("Hybrid backend decoded length mismatch.");
  decodedOutput->setpos(0);
}

} // namespace backend_detail

class RawStoredBackend final : public Backend {
public:
  RouteId route() const override { return RouteId::RAW_STORED; }
  DecoderContractId decoderContract() const override {
    return DecoderContractId::RAW_STORED_V1;
  }
  BackendMetadata inspect(File* decodedInput) const override {
    (void)decodedInput;
    quit("RAW_STORED encoding has been removed.");
  }
  void encode(File* decodedInput, File* payloadOutput,
              const BackendMetadata& expected) const override {
    (void)decodedInput;
    (void)payloadOutput;
    (void)expected;
    quit("RAW_STORED encoding has been removed.");
  }
  void decode(File* payloadInput, File* decodedOutput,
              const FrameHeader& frame) const override {
    backend_detail::decodeIdentityPayload(payloadInput, decodedOutput, frame);
  }
};

// Stage 2 deliberately carries a complete v216/v217 PAQ archive unchanged.
// The existing arithmetic stream therefore remains one indivisible decoder
// contract while the outer container gains explicit routing and boundaries.
class PaqLegacyArchiveBackend final : public Backend {
public:
  RouteId route() const override { return RouteId::PAQ_LEGACY_ARCHIVE; }
  DecoderContractId decoderContract() const override {
    return DecoderContractId::PAQ8PX_LEGACY_ARCHIVE_V1;
  }
  BackendMetadata inspect(File* decodedInput) const override {
    return backend_detail::inspectIdentityPayload(decodedInput);
  }
  void encode(File* decodedInput, File* payloadOutput,
              const BackendMetadata& expected) const override {
    backend_detail::encodeIdentityPayload(decodedInput, payloadOutput, expected);
  }
  void decode(File* payloadInput, File* decodedOutput,
              const FrameHeader& frame) const override {
    backend_detail::decodeIdentityPayload(payloadInput, decodedOutput, frame);
  }
};

class BackendRegistry {
public:
  static const Backend& paqLegacyArchive() {
    static const PaqLegacyArchiveBackend backend;
    return backend;
  }

  static const Backend& requireForEncoding(RouteId route,
                                           DecoderContractId contract) {
    const Backend& paq = paqLegacyArchive();
    if (route == paq.route() && contract == paq.decoderContract())
      return paq;
    if (route == RouteId::RAW_STORED)
      quit("RAW_STORED is not an encoder route.");
    quit("Unsupported hybrid encoder route or decoder contract.");
  }

  static const Backend& requireForDecoding(RouteId route,
                                           DecoderContractId contract) {
    const Backend& raw = rawStored();
    const Backend& paq = paqLegacyArchive();

    if (route == raw.route() && contract == raw.decoderContract())
      return raw;
    if (route == paq.route() && contract == paq.decoderContract())
      return paq;
    quit("Unsupported hybrid route or decoder contract.");
  }

private:
  static const Backend& rawStored() {
    static const RawStoredBackend backend;
    return backend;
  }
};

inline FrameHeader makeFrameHeader(const Backend& backend,
                                   const BackendMetadata& metadata) {
  FrameHeader frame;
  frame.route = backend.route();
  frame.decoderContract = backend.decoderContract();
  frame.decodedLength = metadata.decodedLength;
  frame.payloadLength = metadata.payloadLength;
  frame.payloadCrc32 = metadata.payloadCrc32;
  frame.decodedCrc32 = metadata.decodedCrc32;
  return frame;
}

// Stage 1-2 writer. The frame count is known before the archive header is
// emitted. Every backend first writes to an isolated temporary payload so a
// short/long backend result cannot overwrite a later frame header and the
// exact payloadLength is checked before any frame bytes are committed.
class ArchiveWriter {
public:
  ArchiveWriter(File* archive, uint32_t frameCount)
    : archive_(archive), frameCount_(frameCount), nextFrame_(0),
      totalDecodedLength_(0), totalPayloadLength_(0) {
    if (archive_ == nullptr || archive_->curPos() != 0)
      quit("Hybrid archive output must start empty.");
    archive_->setEnd();
    if (archive_->curPos() != 0)
      quit("Hybrid archive output must be an empty file.");
    archive_->setpos(0);
    writeArchiveHeader(archive_, frameCount_);
  }

  ArchiveWriter(const ArchiveWriter&) = delete;
  ArchiveWriter& operator=(const ArchiveWriter&) = delete;

  FrameHeader writeNext(const Backend& backend, File* decodedInput) {
    if (nextFrame_ >= frameCount_)
      quit("Hybrid archive writer received more frames than declared.");

    // A writer may only publish a route/contract pair that this decoder knows,
    // and the registered implementation defines the actual wire semantics.
    const Backend& registered =
      BackendRegistry::requireForEncoding(backend.route(),
                                          backend.decoderContract());
    const BackendMetadata metadata = registered.inspect(decodedInput);
    const FrameHeader frame = makeFrameHeader(registered, metadata);
    accountFrame(frame);

    FileTmp payload;
    registered.encode(decodedInput, &payload, metadata);
    payload.setEnd();
    if (payload.curPos() != frame.payloadLength)
      quit("Hybrid backend produced a payload with an unexpected length.");
    payload.setpos(0);

    writeFrameHeader(archive_, frame);
    const uint32_t actualPayloadCrc = backend_detail::copyExact(
      &payload, archive_, frame.payloadLength,
      "Hybrid backend payload was truncated before archive framing.");
    if (actualPayloadCrc != frame.payloadCrc32)
      quit("Hybrid backend produced a payload with an unexpected checksum.");
    if (payload.getchar() != EOF)
      quit("Hybrid backend produced bytes beyond the declared payload length.");

    ++nextFrame_;
    return frame;
  }

  void finish() const {
    if (nextFrame_ != frameCount_)
      quit("Hybrid archive writer did not emit every declared frame.");
  }

  uint32_t frameCount() const { return frameCount_; }
  uint32_t framesWritten() const { return nextFrame_; }

private:
  void accountFrame(const FrameHeader& frame) {
    if (frame.decodedLength > kMaximumFrameDataLength ||
        frame.payloadLength > kMaximumFrameDataLength)
      quit("Hybrid frame length is outside the v1 resource limit.");
    if (totalDecodedLength_ > kMaximumFrameDataLength - frame.decodedLength)
      quit("Hybrid archive decoded length exceeds the v1 resource limit.");
    if (totalPayloadLength_ > kMaximumFrameDataLength - frame.payloadLength)
      quit("Hybrid archive payload length exceeds the v1 resource limit.");
    totalDecodedLength_ += frame.decodedLength;
    totalPayloadLength_ += frame.payloadLength;
  }

  File* archive_;
  uint32_t frameCount_;
  uint32_t nextFrame_;
  uint64_t totalDecodedLength_;
  uint64_t totalPayloadLength_;
};

// Sequential reader counterpart. A backend receives exactly payloadLength
// bytes; it can neither consume the next frame header nor silently ignore
// trailing archive data. Registered Stage 0-3 contracts have no recipe bytes.
class ArchiveReader {
public:
  explicit ArchiveReader(File* archive)
    : archive_(archive), header_(readArchiveHeader(archive)), nextFrame_(0),
      totalDecodedLength_(0), totalPayloadLength_(0), endChecked_(false) {}

  ArchiveReader(const ArchiveReader&) = delete;
  ArchiveReader& operator=(const ArchiveReader&) = delete;

  const ArchiveHeader& header() const { return header_; }
  uint32_t framesRead() const { return nextFrame_; }
  bool hasNext() const { return nextFrame_ < header_.frameCount; }

  FrameHeader readNext(File* decodedOutput) {
    return readNextImpl(decodedOutput, false, RouteId::PAQ_LEGACY_ARCHIVE,
                        DecoderContractId::PAQ8PX_LEGACY_ARCHIVE_V1);
  }

  FrameHeader readNext(File* decodedOutput, RouteId expectedRoute,
                       DecoderContractId expectedContract) {
    return readNextImpl(decodedOutput, true, expectedRoute, expectedContract);
  }

  void finish() {
    if (nextFrame_ != header_.frameCount)
      quit("Hybrid archive reader did not consume every declared frame.");
    checkEnd();
  }

private:
  FrameHeader readNextImpl(File* decodedOutput, bool requireExpected,
                           RouteId expectedRoute,
                           DecoderContractId expectedContract) {
    if (!hasNext())
      quit("Hybrid archive reader has no remaining frame.");

    const FrameHeader frame = readFrameHeader(archive_);
    if (requireExpected &&
        (frame.route != expectedRoute ||
         frame.decoderContract != expectedContract))
      quit("Hybrid frame does not match the required decoder contract.");
    const Backend& backend =
      BackendRegistry::requireForDecoding(frame.route, frame.decoderContract);
    accountFrame(frame);
    backend_detail::BoundedInputFile payload(archive_, frame.payloadLength);
    backend.decode(&payload, decodedOutput, frame);
    if (payload.remaining() != 0)
      quit("Hybrid backend did not consume its complete bounded payload.");

    ++nextFrame_;
    if (nextFrame_ == header_.frameCount)
      checkEnd();
    return frame;
  }

  void accountFrame(const FrameHeader& frame) {
    if (frame.decodedLength > kMaximumFrameDataLength ||
        frame.payloadLength > kMaximumFrameDataLength)
      quit("Hybrid frame length is outside the v1 resource limit.");
    if (totalDecodedLength_ > kMaximumFrameDataLength - frame.decodedLength)
      quit("Hybrid archive decoded length exceeds the v1 resource limit.");
    if (totalPayloadLength_ > kMaximumFrameDataLength - frame.payloadLength)
      quit("Hybrid archive payload length exceeds the v1 resource limit.");
    totalDecodedLength_ += frame.decodedLength;
    totalPayloadLength_ += frame.payloadLength;
  }

  void checkEnd() {
    if (!endChecked_) {
      requireEndOfArchive(archive_);
      endChecked_ = true;
    }
  }

  File* archive_;
  ArchiveHeader header_;
  uint32_t nextFrame_;
  uint64_t totalDecodedLength_;
  uint64_t totalPayloadLength_;
  bool endChecked_;
};

inline void writeSingleFrameArchive(File* archive, const Backend& backend,
                                    File* decodedInput) {
  ArchiveWriter writer(archive, 1);
  writer.writeNext(backend, decodedInput);
  writer.finish();
}

inline FrameHeader readSingleFrameArchive(File* archive, File* decodedOutput,
                                          RouteId expectedRoute,
                                          DecoderContractId expectedContract) {
  ArchiveReader reader(archive);
  if (reader.header().frameCount != 1)
    quit("This decoder requires exactly one top-level hybrid frame.");
  const FrameHeader frame =
    reader.readNext(decodedOutput, expectedRoute, expectedContract);
  reader.finish();
  return frame;
}

// Read-only compatibility for hybrid-v1 archives produced before RAW encoding
// was removed. New archives cannot select this route.
inline FrameHeader readRawStoredArchive(File* archive, File* restoredOutput) {
  return readSingleFrameArchive(
    archive, restoredOutput, RouteId::RAW_STORED,
    DecoderContractId::RAW_STORED_V1);
}

inline void writePaqLegacyArchive(File* archive, File* legacyArchiveInput) {
  writeSingleFrameArchive(archive, BackendRegistry::paqLegacyArchive(),
                          legacyArchiveInput);
}

inline FrameHeader readPaqLegacyArchive(File* archive,
                                        File* legacyArchiveOutput) {
  return readSingleFrameArchive(
    archive, legacyArchiveOutput, RouteId::PAQ_LEGACY_ARCHIVE,
    DecoderContractId::PAQ8PX_LEGACY_ARCHIVE_V1);
}

} // namespace hybrid
