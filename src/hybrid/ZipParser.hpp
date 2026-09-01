#pragma once

#include "ContainerLayout.hpp"
#include "RoutedIO.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace routed {

enum class ZipParseStatus : uint8_t {
  OK = 0,
  NOT_ZIP,
  INVALID_ZIP,
  UNSUPPORTED_MULTI_DISK,
  RESOURCE_LIMIT,
  AMBIGUOUS_DATA_DESCRIPTOR
};

struct ZipParseLimits {
  uint64_t maximumEntries = 1024u * 1024u;
  uint32_t maximumEocdCandidates = 4096;
  uint64_t maximumStoredCrcBytes =
    std::numeric_limits<uint64_t>::max();
};

struct ZipParseSummary {
  bool zip64 = false;
  uint64_t entryCount = 0;
  uint64_t storedMemberCount = 0;
  uint64_t opaqueMemberCount = 0;
  uint64_t crcRejectedStoredCount = 0;
  uint64_t centralDirectoryOffset = 0;
  uint64_t centralDirectoryLength = 0;
  uint64_t eocdOffset = 0;
  uint64_t trailingLength = 0;
};

namespace zip_detail {

constexpr uint32_t kLocalHeaderSignature = 0x04034b50u;
constexpr uint32_t kCentralHeaderSignature = 0x02014b50u;
constexpr uint32_t kCentralDigitalSignature = 0x05054b50u;
constexpr uint32_t kEocdSignature = 0x06054b50u;
constexpr uint32_t kZip64EocdSignature = 0x06064b50u;
constexpr uint32_t kZip64LocatorSignature = 0x07064b50u;
constexpr uint32_t kDataDescriptorSignature = 0x08074b50u;
constexpr uint16_t kZip64ExtraId = 0x0001u;

constexpr uint64_t kLocalHeaderBytes = 30;
constexpr uint64_t kCentralHeaderBytes = 46;
constexpr uint64_t kEocdBytes = 22;
constexpr uint64_t kZip64LocatorBytes = 20;
constexpr uint64_t kZip64EocdMinimumBytes = 56;
constexpr size_t kZipScanBufferBytes = 64u * 1024u;

inline uint16_t load16le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) |
         (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t load32le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline uint64_t load64le(const uint8_t* p) {
  return static_cast<uint64_t>(load32le(p)) |
         (static_cast<uint64_t>(load32le(p + 4)) << 32);
}

inline bool checkedAdd(uint64_t left, uint64_t right, uint64_t& result) {
  if (left > std::numeric_limits<uint64_t>::max() - right)
    return false;
  result = left + right;
  return true;
}

inline bool rangeInside(uint64_t offset, uint64_t length, uint64_t limit) {
  uint64_t end = 0;
  return checkedAdd(offset, length, end) && end <= limit;
}

inline bool readAt(File* input, uint64_t offset, uint8_t* destination,
                   uint64_t length) {
  if (input == nullptr || (length != 0 && destination == nullptr))
    return false;
  input->setpos(offset);
  return length == 0 || input->blockRead(destination, length) == length;
}

template <size_t Size>
inline bool readArray(File* input, uint64_t offset,
                      std::array<uint8_t, Size>& destination) {
  return readAt(input, offset, destination.data(), destination.size());
}

class PositionRestorer {
public:
  explicit PositionRestorer(File* file)
    : file_(file), position_(file != nullptr ? file->curPos() : 0) {}

  ~PositionRestorer() {
    if (file_ != nullptr)
      file_->setpos(position_);
  }

private:
  File* file_ = nullptr;
  uint64_t position_ = 0;
};

inline bool rangesEqual(File* input, uint64_t left, uint64_t right,
                        uint64_t length) {
  std::array<uint8_t, kZipScanBufferBytes> leftBuffer{};
  std::array<uint8_t, kZipScanBufferBytes> rightBuffer{};
  uint64_t compared = 0;
  while (compared != length) {
    const uint64_t remaining = length - compared;
    const uint64_t request = remaining < leftBuffer.size()
      ? remaining : leftBuffer.size();
    if (!readAt(input, left + compared, leftBuffer.data(), request) ||
        !readAt(input, right + compared, rightBuffer.data(), request))
      return false;
    for (uint64_t i = 0; i < request; ++i) {
      if (leftBuffer[static_cast<size_t>(i)] !=
          rightBuffer[static_cast<size_t>(i)])
        return false;
    }
    compared += request;
  }
  return true;
}

inline const std::array<uint32_t, 256>& crc32Table() {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> result{};
    for (uint32_t value = 0; value != result.size(); ++value) {
      uint32_t crc = value;
      for (uint32_t bit = 0; bit != 8; ++bit)
        crc = (crc >> 1) ^ ((crc & 1u) != 0 ? 0xedb88320u : 0u);
      result[value] = crc;
    }
    return result;
  }();
  return table;
}

inline bool crc32Range(File* input, uint64_t offset, uint64_t length,
                       uint32_t& result) {
  std::array<uint8_t, kZipScanBufferBytes> buffer{};
  uint64_t processed = 0;
  uint32_t crc = 0xffffffffu;
  const std::array<uint32_t, 256>& table = crc32Table();
  while (processed != length) {
    const uint64_t remaining = length - processed;
    const uint64_t request = remaining < buffer.size()
      ? remaining : buffer.size();
    if (!readAt(input, offset + processed, buffer.data(), request))
      return false;
    for (uint64_t i = 0; i < request; ++i)
      crc = table[(crc ^ buffer[static_cast<size_t>(i)]) & 0xffu] ^
            (crc >> 8);
    processed += request;
  }
  result = crc ^ 0xffffffffu;
  return true;
}

struct Zip64Values {
  bool hasUncompressedSize = false;
  bool hasCompressedSize = false;
  bool hasLocalHeaderOffset = false;
  bool hasDiskStart = false;
  uint64_t uncompressedSize = 0;
  uint64_t compressedSize = 0;
  uint64_t localHeaderOffset = 0;
  uint32_t diskStart = 0;
};

// ZIP64 values occur in the order of the corresponding sentinel fields in the
// owning central/local header.  All extra-field TLVs are also structurally
// validated so a malformed unknown field cannot shift the interpretation.
inline bool parseZip64Extra(
    const std::vector<uint8_t>& extra,
    bool needUncompressedSize,
    bool needCompressedSize,
    bool needLocalHeaderOffset,
    bool needDiskStart,
    Zip64Values& values) {
  values = {};
  size_t cursor = 0;
  bool foundZip64 = false;
  while (cursor != extra.size()) {
    if (extra.size() - cursor < 4)
      return false;
    const uint16_t id = load16le(extra.data() + cursor);
    const uint16_t dataLength = load16le(extra.data() + cursor + 2);
    cursor += 4;
    if (dataLength > extra.size() - cursor)
      return false;
    if (id == kZip64ExtraId) {
      if (foundZip64)
        return false;
      foundZip64 = true;
      size_t fieldCursor = cursor;
      const size_t fieldEnd = cursor + dataLength;
      const auto read64 = [&](uint64_t& output) -> bool {
        if (fieldEnd - fieldCursor < 8)
          return false;
        output = load64le(extra.data() + fieldCursor);
        fieldCursor += 8;
        return true;
      };
      if (needUncompressedSize) {
        if (!read64(values.uncompressedSize))
          return false;
        values.hasUncompressedSize = true;
      }
      if (needCompressedSize) {
        if (!read64(values.compressedSize))
          return false;
        values.hasCompressedSize = true;
      }
      if (needLocalHeaderOffset) {
        if (!read64(values.localHeaderOffset))
          return false;
        values.hasLocalHeaderOffset = true;
      }
      if (needDiskStart) {
        if (fieldEnd - fieldCursor < 4)
          return false;
        values.diskStart = load32le(extra.data() + fieldCursor);
        fieldCursor += 4;
        values.hasDiskStart = true;
      }
      // Remaining bytes are allowed by the extensible ZIP64 extra-field
      // contract, but required values must have appeared in canonical order.
    }
    cursor += dataLength;
  }
  return (!needUncompressedSize || values.hasUncompressedSize) &&
         (!needCompressedSize || values.hasCompressedSize) &&
         (!needLocalHeaderOffset || values.hasLocalHeaderOffset) &&
         (!needDiskStart || values.hasDiskStart);
}

struct EndRecord {
  bool zip64 = false;
  uint64_t eocdOffset = 0;
  uint64_t eocdEnd = 0;
  uint64_t directoryOffset = 0;
  uint64_t directoryLength = 0;
  uint64_t entryCount = 0;
  uint64_t directoryLimit = 0;
};

inline ZipParseStatus parseEndRecord(File* input, uint64_t inputLength,
                                     uint64_t eocdOffset,
                                     EndRecord& result) {
  std::array<uint8_t, kEocdBytes> eocd{};
  if (!rangeInside(eocdOffset, eocd.size(), inputLength) ||
      !readArray(input, eocdOffset, eocd) ||
      load32le(eocd.data()) != kEocdSignature)
    return ZipParseStatus::INVALID_ZIP;

  const uint16_t diskNumber = load16le(eocd.data() + 4);
  const uint16_t directoryDisk = load16le(eocd.data() + 6);
  const uint16_t entriesOnDisk16 = load16le(eocd.data() + 8);
  const uint16_t entryCount16 = load16le(eocd.data() + 10);
  const uint32_t directoryLength32 = load32le(eocd.data() + 12);
  const uint32_t directoryOffset32 = load32le(eocd.data() + 16);
  const uint16_t commentLength = load16le(eocd.data() + 20);
  if (!checkedAdd(eocdOffset, kEocdBytes + commentLength, result.eocdEnd) ||
      result.eocdEnd > inputLength)
    return ZipParseStatus::INVALID_ZIP;
  if (diskNumber != 0 || directoryDisk != 0 ||
      entriesOnDisk16 != entryCount16)
    return ZipParseStatus::UNSUPPORTED_MULTI_DISK;

  const bool hasSentinel = entriesOnDisk16 == 0xffffu ||
                           entryCount16 == 0xffffu ||
                           directoryLength32 == 0xffffffffu ||
                           directoryOffset32 == 0xffffffffu;
  bool hasLocator = false;
  std::array<uint8_t, kZip64LocatorBytes> locator{};
  const uint64_t locatorOffset = eocdOffset >= kZip64LocatorBytes
    ? eocdOffset - kZip64LocatorBytes : 0;
  if (eocdOffset >= kZip64LocatorBytes &&
      readArray(input, locatorOffset, locator) &&
      load32le(locator.data()) == kZip64LocatorSignature)
    hasLocator = true;

  if (!hasSentinel && !hasLocator) {
    result.zip64 = false;
    result.eocdOffset = eocdOffset;
    result.directoryOffset = directoryOffset32;
    result.directoryLength = directoryLength32;
    result.entryCount = entryCount16;
    result.directoryLimit = eocdOffset;
  } else {
    if (!hasLocator)
      return ZipParseStatus::INVALID_ZIP;
    const uint32_t zip64EocdDisk = load32le(locator.data() + 4);
    const uint64_t zip64EocdOffset = load64le(locator.data() + 8);
    const uint32_t totalDisks = load32le(locator.data() + 16);
    if (zip64EocdDisk != 0 || totalDisks != 1)
      return ZipParseStatus::UNSUPPORTED_MULTI_DISK;
    std::array<uint8_t, kZip64EocdMinimumBytes> zip64{};
    if (!rangeInside(zip64EocdOffset, zip64.size(), locatorOffset) ||
        !readArray(input, zip64EocdOffset, zip64) ||
        load32le(zip64.data()) != kZip64EocdSignature)
      return ZipParseStatus::INVALID_ZIP;
    const uint64_t recordPayloadLength = load64le(zip64.data() + 4);
    uint64_t zip64RecordEnd = 0;
    if (recordPayloadLength < 44 ||
        !checkedAdd(zip64EocdOffset, 12, zip64RecordEnd) ||
        !checkedAdd(zip64RecordEnd, recordPayloadLength, zip64RecordEnd) ||
        zip64RecordEnd > locatorOffset)
      return ZipParseStatus::INVALID_ZIP;
    const uint32_t zip64Disk = load32le(zip64.data() + 16);
    const uint32_t zip64DirectoryDisk = load32le(zip64.data() + 20);
    const uint64_t entriesOnDisk64 = load64le(zip64.data() + 24);
    const uint64_t entryCount64 = load64le(zip64.data() + 32);
    if (zip64Disk != 0 || zip64DirectoryDisk != 0 ||
        entriesOnDisk64 != entryCount64)
      return ZipParseStatus::UNSUPPORTED_MULTI_DISK;

    const uint64_t directoryLength64 = load64le(zip64.data() + 40);
    const uint64_t directoryOffset64 = load64le(zip64.data() + 48);
    if (entriesOnDisk16 != 0xffffu && entriesOnDisk16 != entriesOnDisk64)
      return ZipParseStatus::INVALID_ZIP;
    if (entryCount16 != 0xffffu && entryCount16 != entryCount64)
      return ZipParseStatus::INVALID_ZIP;
    if (directoryLength32 != 0xffffffffu &&
        directoryLength32 != directoryLength64)
      return ZipParseStatus::INVALID_ZIP;
    if (directoryOffset32 != 0xffffffffu &&
        directoryOffset32 != directoryOffset64)
      return ZipParseStatus::INVALID_ZIP;

    result.zip64 = true;
    result.eocdOffset = eocdOffset;
    result.directoryOffset = directoryOffset64;
    result.directoryLength = directoryLength64;
    result.entryCount = entryCount64;
    result.directoryLimit = zip64EocdOffset;
  }

  uint64_t directoryEnd = 0;
  if (!checkedAdd(result.directoryOffset, result.directoryLength,
                  directoryEnd) ||
      directoryEnd > result.directoryLimit)
    return ZipParseStatus::INVALID_ZIP;
  return ZipParseStatus::OK;
}

struct CentralEntry {
  uint32_t index = 0;
  uint16_t versionNeeded = 0;
  uint16_t flags = 0;
  uint16_t method = 0;
  uint32_t crc32 = 0;
  uint64_t compressedSize = 0;
  uint64_t uncompressedSize = 0;
  uint64_t localHeaderOffset = 0;
  uint64_t centralNameOffset = 0;
  uint16_t nameLength = 0;
  bool zip64Sizes = false;

  uint64_t localStart = 0;
  uint64_t dataStart = 0;
  uint64_t dataEnd = 0;
  uint64_t localRecordEnd = 0;
  bool storedEligible = false;
  bool storedCrcRejected = false;
};

inline ZipParseStatus parseCentralDirectory(
    File* input, uint64_t inputLength, const EndRecord& end,
    const ZipParseLimits& limits, std::vector<CentralEntry>& entries) {
  if (end.entryCount > limits.maximumEntries ||
      end.entryCount > std::numeric_limits<uint32_t>::max() ||
      end.entryCount > std::numeric_limits<size_t>::max())
    return ZipParseStatus::RESOURCE_LIMIT;
  entries.clear();
  entries.reserve(static_cast<size_t>(end.entryCount));
  uint64_t directoryEnd = end.directoryOffset + end.directoryLength;
  uint64_t cursor = end.directoryOffset;

  for (uint64_t index = 0; index != end.entryCount; ++index) {
    std::array<uint8_t, kCentralHeaderBytes> header{};
    if (!rangeInside(cursor, header.size(), directoryEnd) ||
        !readArray(input, cursor, header) ||
        load32le(header.data()) != kCentralHeaderSignature)
      return ZipParseStatus::INVALID_ZIP;
    const uint16_t nameLength = load16le(header.data() + 28);
    const uint16_t extraLength = load16le(header.data() + 30);
    const uint16_t commentLength = load16le(header.data() + 32);
    uint64_t recordLength = kCentralHeaderBytes;
    if (!checkedAdd(recordLength, nameLength, recordLength) ||
        !checkedAdd(recordLength, extraLength, recordLength) ||
        !checkedAdd(recordLength, commentLength, recordLength) ||
        !rangeInside(cursor, recordLength, directoryEnd))
      return ZipParseStatus::INVALID_ZIP;

    CentralEntry entry;
    entry.index = static_cast<uint32_t>(index);
    entry.versionNeeded = load16le(header.data() + 6);
    entry.flags = load16le(header.data() + 8);
    entry.method = load16le(header.data() + 10);
    entry.crc32 = load32le(header.data() + 16);
    const uint32_t compressed32 = load32le(header.data() + 20);
    const uint32_t uncompressed32 = load32le(header.data() + 24);
    const uint16_t diskStart16 = load16le(header.data() + 34);
    const uint32_t localOffset32 = load32le(header.data() + 42);
    entry.centralNameOffset = cursor + kCentralHeaderBytes;
    entry.nameLength = nameLength;

    const uint64_t extraOffset = entry.centralNameOffset + nameLength;
    std::vector<uint8_t> extra(extraLength);
    if (extraLength != 0 &&
        !readAt(input, extraOffset, extra.data(), extra.size()))
      return ZipParseStatus::INVALID_ZIP;
    const bool needUncompressed = uncompressed32 == 0xffffffffu;
    const bool needCompressed = compressed32 == 0xffffffffu;
    const bool needLocalOffset = localOffset32 == 0xffffffffu;
    const bool needDiskStart = diskStart16 == 0xffffu;
    Zip64Values zip64;
    if (!parseZip64Extra(extra, needUncompressed, needCompressed,
                         needLocalOffset, needDiskStart, zip64))
      return ZipParseStatus::INVALID_ZIP;
    entry.uncompressedSize = needUncompressed
      ? zip64.uncompressedSize : uncompressed32;
    entry.compressedSize = needCompressed
      ? zip64.compressedSize : compressed32;
    entry.localHeaderOffset = needLocalOffset
      ? zip64.localHeaderOffset : localOffset32;
    const uint32_t diskStart = needDiskStart ? zip64.diskStart : diskStart16;
    if (diskStart != 0)
      return ZipParseStatus::UNSUPPORTED_MULTI_DISK;
    entry.zip64Sizes = needUncompressed || needCompressed;
    entries.push_back(entry);
    cursor += recordLength;
  }

  if (cursor != directoryEnd) {
    std::array<uint8_t, 6> signature{};
    if (!rangeInside(cursor, signature.size(), directoryEnd) ||
        !readArray(input, cursor, signature) ||
        load32le(signature.data()) != kCentralDigitalSignature)
      return ZipParseStatus::INVALID_ZIP;
    const uint16_t signatureLength = load16le(signature.data() + 4);
    if (!rangeInside(cursor, uint64_t{6} + signatureLength, directoryEnd) ||
        cursor + 6 + signatureLength != directoryEnd)
      return ZipParseStatus::INVALID_ZIP;
  }
  (void)inputLength;
  return ZipParseStatus::OK;
}

inline bool descriptorCandidateMatches(
    File* input, uint64_t descriptorOffset, bool hasSignature, bool zip64,
    const CentralEntry& entry, uint64_t inputLimit, uint64_t& length) {
  const uint64_t bodyLength = zip64 ? 20 : 12;
  length = bodyLength + (hasSignature ? 4 : 0);
  if (!rangeInside(descriptorOffset, length, inputLimit))
    return false;
  std::array<uint8_t, 24> bytes{};
  if (!readAt(input, descriptorOffset, bytes.data(), length))
    return false;
  size_t cursor = 0;
  if (hasSignature) {
    if (load32le(bytes.data()) != kDataDescriptorSignature)
      return false;
    cursor += 4;
  }
  const uint32_t crc = load32le(bytes.data() + cursor);
  cursor += 4;
  uint64_t compressed = 0;
  uint64_t uncompressed = 0;
  if (zip64) {
    compressed = load64le(bytes.data() + cursor);
    uncompressed = load64le(bytes.data() + cursor + 8);
  } else {
    if (entry.compressedSize > std::numeric_limits<uint32_t>::max() ||
        entry.uncompressedSize > std::numeric_limits<uint32_t>::max())
      return false;
    compressed = load32le(bytes.data() + cursor);
    uncompressed = load32le(bytes.data() + cursor + 4);
  }
  return crc == entry.crc32 && compressed == entry.compressedSize &&
         uncompressed == entry.uncompressedSize;
}

inline ZipParseStatus parseDataDescriptor(
    File* input, uint64_t descriptorOffset, uint64_t inputLimit,
    const CentralEntry& entry, uint64_t& descriptorLength) {
  uint32_t matches = 0;
  uint64_t matchedLength = 0;
  const bool sizesRequireZip64 =
    entry.compressedSize > std::numeric_limits<uint32_t>::max() ||
    entry.uncompressedSize > std::numeric_limits<uint32_t>::max();
  for (uint32_t zip64Value = 0; zip64Value != 2; ++zip64Value) {
    const bool zip64 = zip64Value != 0;
    // A central ZIP64 size sentinel fixes descriptor width at 64 bits.  With
    // ordinary central sizes, accept either published descriptor width only
    // when its values select one representation uniquely.
    if ((!zip64 && (entry.zip64Sizes || sizesRequireZip64)) ||
        (zip64 && !entry.zip64Sizes && sizesRequireZip64 == false &&
         entry.versionNeeded < 45))
      continue;
    for (uint32_t signedValue = 0; signedValue != 2; ++signedValue) {
      uint64_t candidateLength = 0;
      if (descriptorCandidateMatches(input, descriptorOffset,
                                     signedValue != 0, zip64, entry,
                                     inputLimit, candidateLength)) {
        ++matches;
        matchedLength = candidateLength;
      }
    }
  }
  if (matches == 0)
    return ZipParseStatus::INVALID_ZIP;
  if (matches != 1)
    return ZipParseStatus::AMBIGUOUS_DATA_DESCRIPTOR;
  descriptorLength = matchedLength;
  return ZipParseStatus::OK;
}

inline ZipParseStatus parseLocalRecord(
    File* input, uint64_t inputLength, uint64_t directoryOffset,
    const ZipParseLimits& limits, uint64_t& crcBudget,
    CentralEntry& entry) {
  entry.localStart = entry.localHeaderOffset;
  std::array<uint8_t, kLocalHeaderBytes> header{};
  if (!rangeInside(entry.localStart, header.size(), directoryOffset) ||
      !readArray(input, entry.localStart, header) ||
      load32le(header.data()) != kLocalHeaderSignature)
    return ZipParseStatus::INVALID_ZIP;
  const uint16_t localFlags = load16le(header.data() + 6);
  const uint16_t localMethod = load16le(header.data() + 8);
  const uint32_t localCrc = load32le(header.data() + 14);
  const uint32_t localCompressed32 = load32le(header.data() + 18);
  const uint32_t localUncompressed32 = load32le(header.data() + 22);
  const uint16_t localNameLength = load16le(header.data() + 26);
  const uint16_t localExtraLength = load16le(header.data() + 28);
  if (localFlags != entry.flags || localMethod != entry.method ||
      localNameLength != entry.nameLength)
    return ZipParseStatus::INVALID_ZIP;

  uint64_t localNameOffset = entry.localStart + kLocalHeaderBytes;
  uint64_t localExtraOffset = 0;
  if (!checkedAdd(localNameOffset, localNameLength, localExtraOffset) ||
      !checkedAdd(localExtraOffset, localExtraLength, entry.dataStart) ||
      entry.dataStart > directoryOffset ||
      !rangesEqual(input, localNameOffset, entry.centralNameOffset,
                   localNameLength))
    return ZipParseStatus::INVALID_ZIP;

  std::vector<uint8_t> localExtra(localExtraLength);
  if (localExtraLength != 0 &&
      !readAt(input, localExtraOffset, localExtra.data(), localExtra.size()))
    return ZipParseStatus::INVALID_ZIP;
  Zip64Values localZip64;
  const bool localNeedUncompressed = localUncompressed32 == 0xffffffffu;
  const bool localNeedCompressed = localCompressed32 == 0xffffffffu;
  if (!parseZip64Extra(localExtra, localNeedUncompressed,
                       localNeedCompressed, false, false, localZip64))
    return ZipParseStatus::INVALID_ZIP;

  if (!checkedAdd(entry.dataStart, entry.compressedSize, entry.dataEnd) ||
      entry.dataEnd > directoryOffset || entry.dataEnd > inputLength)
    return ZipParseStatus::INVALID_ZIP;
  entry.localRecordEnd = entry.dataEnd;

  const bool usesDescriptor = (entry.flags & 0x0008u) != 0;
  if (usesDescriptor) {
    uint64_t descriptorLength = 0;
    const ZipParseStatus descriptorStatus = parseDataDescriptor(
      input, entry.dataEnd, directoryOffset, entry, descriptorLength);
    if (descriptorStatus != ZipParseStatus::OK)
      return descriptorStatus;
    if (!checkedAdd(entry.dataEnd, descriptorLength,
                    entry.localRecordEnd))
      return ZipParseStatus::INVALID_ZIP;
  } else {
    const uint64_t localCompressed = localNeedCompressed
      ? localZip64.compressedSize : localCompressed32;
    const uint64_t localUncompressed = localNeedUncompressed
      ? localZip64.uncompressedSize : localUncompressed32;
    if (localCrc != entry.crc32 ||
        localCompressed != entry.compressedSize ||
        localUncompressed != entry.uncompressedSize)
      return ZipParseStatus::INVALID_ZIP;
  }

  constexpr uint16_t encryptionFlags = 0x0001u | 0x0040u | 0x2000u;
  const bool storedCandidate = entry.method == 0 &&
                               (entry.flags & encryptionFlags) == 0 &&
                               entry.compressedSize == entry.uncompressedSize;
  if (storedCandidate) {
    if (entry.compressedSize > limits.maximumStoredCrcBytes - crcBudget)
      return ZipParseStatus::RESOURCE_LIMIT;
    crcBudget += entry.compressedSize;
    uint32_t actualCrc = 0;
    if (!crc32Range(input, entry.dataStart, entry.compressedSize, actualCrc))
      return ZipParseStatus::INVALID_ZIP;
    entry.storedEligible = actualCrc == entry.crc32;
    entry.storedCrcRejected = !entry.storedEligible;
  }
  return ZipParseStatus::OK;
}

inline ZipParseStatus buildLayout(
    uint64_t absoluteSourceOffset, uint64_t inputLength,
    const EndRecord& end, std::vector<CentralEntry>& entries,
    ContainerLayout& output, ZipParseSummary* summary) {
  std::sort(entries.begin(), entries.end(),
            [](const CentralEntry& left, const CentralEntry& right) {
              if (left.localStart != right.localStart)
                return left.localStart < right.localStart;
              return left.index < right.index;
            });

  uint64_t previousEnd = 0;
  bool first = true;
  for (const CentralEntry& entry : entries) {
    if ((!first && entry.localStart < previousEnd) ||
        entry.localRecordEnd > end.directoryOffset ||
        entry.localStart >= entry.localRecordEnd)
      return ZipParseStatus::INVALID_ZIP;
    previousEnd = entry.localRecordEnd;
    first = false;
  }

  ContainerLayout candidate;
  if (!candidate.begin(ContainerKind::ZIP, absoluteSourceOffset, inputLength))
    return ZipParseStatus::INVALID_ZIP;
  uint64_t cursor = 0;
  uint64_t storedCount = 0;
  uint64_t opaqueCount = 0;
  uint64_t crcRejected = 0;
  for (const CentralEntry& entry : entries) {
    if (entry.localStart > cursor &&
        !candidate.appendStructure(absoluteSourceOffset + cursor,
                                   entry.localStart - cursor))
      return ZipParseStatus::INVALID_ZIP;

    if (entry.storedEligible && entry.compressedSize != 0) {
      if (entry.dataStart > entry.localStart &&
          !candidate.appendStructure(
            absoluteSourceOffset + entry.localStart,
            entry.dataStart - entry.localStart))
        return ZipParseStatus::INVALID_ZIP;
      ContainerSpan data;
      data.sourceOffset = absoluteSourceOffset + entry.dataStart;
      data.sourceLength = entry.compressedSize;
      data.kind = ContainerSpanKind::STORED_MEMBER_DATA;
      data.memberIndex = entry.index;
      data.method = entry.method;
      data.generalPurposeFlags = entry.flags;
      data.expectedCrc32 = entry.crc32;
      if (!candidate.append(data))
        return ZipParseStatus::INVALID_ZIP;
      if (entry.localRecordEnd > entry.dataEnd &&
          !candidate.appendStructure(
            absoluteSourceOffset + entry.dataEnd,
            entry.localRecordEnd - entry.dataEnd))
        return ZipParseStatus::INVALID_ZIP;
      ++storedCount;
    } else if (entry.storedEligible) {
      // Empty stored members have no data interval to expose.  Preserve the
      // complete local record as ordinary structure.
      if (!candidate.appendStructure(
            absoluteSourceOffset + entry.localStart,
            entry.localRecordEnd - entry.localStart))
        return ZipParseStatus::INVALID_ZIP;
      ++storedCount;
    } else {
      ContainerSpan opaque;
      opaque.sourceOffset = absoluteSourceOffset + entry.localStart;
      opaque.sourceLength = entry.localRecordEnd - entry.localStart;
      opaque.kind = ContainerSpanKind::OPAQUE_MEMBER;
      opaque.memberIndex = entry.index;
      opaque.method = entry.method;
      opaque.generalPurposeFlags = entry.flags;
      opaque.expectedCrc32 = entry.crc32;
      if (!candidate.append(opaque))
        return ZipParseStatus::INVALID_ZIP;
      ++opaqueCount;
      if (entry.storedCrcRejected)
        ++crcRejected;
    }
    cursor = entry.localRecordEnd;
  }
  if (cursor < inputLength &&
      !candidate.appendStructure(absoluteSourceOffset + cursor,
                                 inputLength - cursor))
    return ZipParseStatus::INVALID_ZIP;
  if (!candidate.seal() || !candidate.validCoverage())
    return ZipParseStatus::INVALID_ZIP;

  output = candidate;
  if (summary != nullptr) {
    summary->zip64 = end.zip64;
    summary->entryCount = end.entryCount;
    summary->storedMemberCount = storedCount;
    summary->opaqueMemberCount = opaqueCount;
    summary->crcRejectedStoredCount = crcRejected;
    summary->centralDirectoryOffset =
      absoluteSourceOffset + end.directoryOffset;
    summary->centralDirectoryLength = end.directoryLength;
    summary->eocdOffset = absoluteSourceOffset + end.eocdOffset;
    summary->trailingLength = inputLength - end.eocdEnd;
  }
  return ZipParseStatus::OK;
}

inline ZipParseStatus parseCandidate(
    File* input, uint64_t absoluteSourceOffset, uint64_t inputLength,
    uint64_t eocdOffset, const ZipParseLimits& limits,
    ContainerLayout& output, ZipParseSummary* summary,
    bool& centralDirectoryRecognized) {
  centralDirectoryRecognized = false;
  EndRecord end;
  ZipParseStatus status = parseEndRecord(input, inputLength, eocdOffset, end);
  if (status != ZipParseStatus::OK)
    return status;
  if (end.entryCount > limits.maximumEntries)
    return ZipParseStatus::RESOURCE_LIMIT;

  std::vector<CentralEntry> entries;
  status = parseCentralDirectory(input, inputLength, end, limits, entries);
  if (status != ZipParseStatus::OK)
    return status;
  centralDirectoryRecognized = true;

  uint64_t crcBudget = 0;
  for (CentralEntry& entry : entries) {
    status = parseLocalRecord(input, inputLength, end.directoryOffset,
                              limits, crcBudget, entry);
    if (status != ZipParseStatus::OK)
      return status;
  }
  return buildLayout(absoluteSourceOffset, inputLength, end, entries,
                     output, summary);
}

inline ZipParseStatus findEocdCandidates(
    File* input, uint64_t inputLength, const ZipParseLimits& limits,
    std::vector<uint64_t>& candidates) {
  candidates.clear();
  if (inputLength < kEocdBytes)
    return ZipParseStatus::NOT_ZIP;
  std::array<uint8_t, kZipScanBufferBytes> buffer{};
  uint64_t offset = 0;
  while (offset + 4 <= inputLength) {
    const uint64_t remaining = inputLength - offset;
    const uint64_t request = remaining < buffer.size()
      ? remaining : buffer.size();
    if (request < 4 || !readAt(input, offset, buffer.data(), request))
      return ZipParseStatus::INVALID_ZIP;
    for (uint64_t index = 0; index + 4 <= request; ++index) {
      if (load32le(buffer.data() + static_cast<size_t>(index)) ==
            kEocdSignature &&
          offset + index + kEocdBytes <= inputLength) {
        if (candidates.size() >= limits.maximumEocdCandidates)
          return ZipParseStatus::RESOURCE_LIMIT;
        candidates.push_back(offset + index);
      }
    }
    if (request == remaining)
      break;
    offset += request - 3; // retain the possible cross-buffer signature
  }
  return candidates.empty() ? ZipParseStatus::NOT_ZIP : ZipParseStatus::OK;
}

} // namespace zip_detail

// Parses one complete byte interval as a conservative single-disk ZIP/ZIP64
// archive.  The caller's File position is restored on every return path.
// Success never copies source bytes: output consists solely of absolute source
// spans and is therefore safe to lower into ordinary PAQ/expert CommitUnits.
inline ZipParseStatus parseZipLayout(
    File* source, uint64_t sourceOffset, uint64_t sourceLength,
    ContainerLayout& output, ZipParseSummary* summary = nullptr,
    const ZipParseLimits& limits = ZipParseLimits{}) {
  output.clear();
  if (summary != nullptr)
    *summary = {};
  if (source == nullptr ||
      sourceOffset > std::numeric_limits<uint64_t>::max() - sourceLength ||
      limits.maximumEntries == 0 || limits.maximumEocdCandidates == 0)
    return ZipParseStatus::INVALID_ZIP;

  zip_detail::PositionRestorer restore(source);
  FileRangeView input(source, sourceOffset, sourceLength);
  std::vector<uint64_t> candidates;
  ZipParseStatus status = zip_detail::findEocdCandidates(
    &input, sourceLength, limits, candidates);
  if (status != ZipParseStatus::OK)
    return status;

  ZipParseStatus strongestFailure = ZipParseStatus::INVALID_ZIP;
  for (auto iterator = candidates.rbegin(); iterator != candidates.rend();
       ++iterator) {
    bool directoryRecognized = false;
    ContainerLayout candidate;
    ZipParseSummary candidateSummary;
    status = zip_detail::parseCandidate(
      &input, sourceOffset, sourceLength, *iterator, limits, candidate,
      summary != nullptr ? &candidateSummary : nullptr,
      directoryRecognized);
    if (status == ZipParseStatus::OK) {
      output = candidate;
      if (summary != nullptr)
        *summary = candidateSummary;
      return status;
    }

    // Once a complete central directory was recognized, a local-record,
    // descriptor or resource failure belongs to that archive rather than to a
    // coincidental signature in trailing bytes.  Do not reinterpret an older
    // embedded EOCD as the root archive.
    if (directoryRecognized ||
        status == ZipParseStatus::AMBIGUOUS_DATA_DESCRIPTOR)
      return status;
    if (status == ZipParseStatus::RESOURCE_LIMIT ||
        status == ZipParseStatus::UNSUPPORTED_MULTI_DISK)
      strongestFailure = status;
  }
  return strongestFailure;
}

} // namespace routed
