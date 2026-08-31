#pragma once

#include "HybridFormat.hpp"

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
  static const Backend& require(RouteId route, DecoderContractId contract) {
    static const RawStoredBackend rawStored;
    static const PaqLegacyArchiveBackend paqLegacy;

    if (route == rawStored.route() && contract == rawStored.decoderContract())
      return rawStored;
    if (route == paqLegacy.route() && contract == paqLegacy.decoderContract())
      return paqLegacy;
    quit("Unsupported hybrid route or decoder contract.");
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

inline void writeSingleFrameArchive(File* archive, const Backend& backend,
                                    File* decodedInput) {
  if (archive == nullptr || archive->curPos() != 0)
    quit("Single-frame hybrid archive output must start empty.");
  const BackendMetadata metadata = backend.inspect(decodedInput);
  const FrameHeader frame = makeFrameHeader(backend, metadata);
  writeArchiveHeader(archive, 1);
  writeFrameHeader(archive, frame);
  backend.encode(decodedInput, archive, metadata);
}

inline FrameHeader readSingleFrameArchive(File* archive, File* decodedOutput,
                                          RouteId expectedRoute,
                                          DecoderContractId expectedContract) {
  const ArchiveHeader header = readArchiveHeader(archive);
  if (header.frameCount != 1)
    quit("This decoder requires exactly one top-level hybrid frame.");
  const FrameHeader frame = readFrameHeader(archive);
  if (frame.route != expectedRoute || frame.decoderContract != expectedContract)
    quit("Hybrid frame does not match the required top-level decoder contract.");
  const Backend& backend = BackendRegistry::require(frame.route, frame.decoderContract);
  backend.decode(archive, decodedOutput, frame);
  requireEndOfArchive(archive);
  return frame;
}

} // namespace hybrid
