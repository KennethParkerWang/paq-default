#pragma once

#include "../file/File.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace routed {

enum class ExecutableKind : uint8_t {
  PE_COFF = 1,
  ELF = 2,
  MACH_O = 3
};

enum class ExecutableIsa : uint8_t {
  NONE = 0,
  X86_32 = 1,
  X86_64 = 2
};

enum class ExecutableSpanKind : uint8_t {
  OPAQUE_DATA = 0,
  CODE = 1
};

enum class ExecutableParseStatus : uint8_t {
  SUCCESS = 0,
  NOT_EXECUTABLE = 1,
  UNSUPPORTED = 2,
  MALFORMED = 3
};

struct ExecutableSpan {
  uint64_t sourceOffset = 0;
  uint64_t sourceLength = 0;
  ExecutableSpanKind kind = ExecutableSpanKind::OPAQUE_DATA;
  ExecutableIsa isa = ExecutableIsa::NONE;
  bool hasVirtualAddress = false;
  uint64_t virtualAddress = 0;

  bool isCode() const { return kind == ExecutableSpanKind::CODE; }
};

struct ExecutableCodeRegion {
  uint64_t sourceOffset = 0;
  uint64_t sourceLength = 0;
  ExecutableIsa isa = ExecutableIsa::NONE;
  uint64_t virtualAddress = 0;
};

// Encoder-side source geometry for an executable image.  Every byte of the
// inspected source interval occurs in exactly one output span.  Only regions
// proven to be x86/x64 code by the native format metadata are marked CODE;
// all headers, padding, data, overlays and unrecognized contents remain
// OPAQUE_DATA and therefore stay on the ordinary PAQ path.
class ExecutableLayout {
public:
  void clear() {
    kind_ = ExecutableKind::PE_COFF;
    isa_ = ExecutableIsa::NONE;
    sourceOffset_ = 0;
    sourceLength_ = 0;
    imageBaseKnown_ = false;
    imageBase_ = 0;
    entryPointKnown_ = false;
    entryPointAddress_ = 0;
    begun_ = false;
    sealed_ = false;
    spans_.clear();
  }

  bool begin(ExecutableKind kind, ExecutableIsa isa,
             uint64_t sourceOffset, uint64_t sourceLength) {
    clear();
    if (isa == ExecutableIsa::NONE || sourceLength == 0 ||
        sourceOffset > std::numeric_limits<uint64_t>::max() - sourceLength)
      return false;
    kind_ = kind;
    isa_ = isa;
    sourceOffset_ = sourceOffset;
    sourceLength_ = sourceLength;
    begun_ = true;
    return true;
  }

  bool setImageBase(uint64_t imageBase) {
    if (!begun_ || sealed_)
      return false;
    imageBaseKnown_ = true;
    imageBase_ = imageBase;
    return true;
  }

  bool setEntryPoint(uint64_t entryPointAddress) {
    if (!begun_ || sealed_)
      return false;
    entryPointKnown_ = true;
    entryPointAddress_ = entryPointAddress;
    return true;
  }

  bool seal(std::vector<ExecutableCodeRegion> regions) {
    if (!begun_ || sealed_)
      return false;
    std::sort(regions.begin(), regions.end(),
              [](const ExecutableCodeRegion& a,
                 const ExecutableCodeRegion& b) {
                if (a.sourceOffset != b.sourceOffset)
                  return a.sourceOffset < b.sourceOffset;
                return a.sourceLength < b.sourceLength;
              });

    const uint64_t sourceEnd = sourceOffset_ + sourceLength_;
    uint64_t cursor = sourceOffset_;
    for (const ExecutableCodeRegion& region : regions) {
      if (region.sourceLength == 0 || region.isa != isa_ ||
          region.sourceOffset < cursor || region.sourceOffset < sourceOffset_ ||
          region.sourceOffset > std::numeric_limits<uint64_t>::max() -
            region.sourceLength ||
          region.sourceOffset + region.sourceLength > sourceEnd ||
          region.virtualAddress > std::numeric_limits<uint64_t>::max() -
            region.sourceLength)
        return false;

      if (cursor < region.sourceOffset) {
        ExecutableSpan opaque;
        opaque.sourceOffset = cursor;
        opaque.sourceLength = region.sourceOffset - cursor;
        spans_.push_back(opaque);
      }

      ExecutableSpan code;
      code.sourceOffset = region.sourceOffset;
      code.sourceLength = region.sourceLength;
      code.kind = ExecutableSpanKind::CODE;
      code.isa = region.isa;
      code.hasVirtualAddress = true;
      code.virtualAddress = region.virtualAddress;
      spans_.push_back(code);
      cursor = region.sourceOffset + region.sourceLength;
    }

    if (cursor < sourceEnd) {
      ExecutableSpan opaque;
      opaque.sourceOffset = cursor;
      opaque.sourceLength = sourceEnd - cursor;
      spans_.push_back(opaque);
    }
    sealed_ = true;
    return validCoverage();
  }

  ExecutableKind kind() const { return kind_; }
  ExecutableIsa isa() const { return isa_; }
  uint64_t sourceOffset() const { return sourceOffset_; }
  uint64_t sourceLength() const { return sourceLength_; }
  bool sealed() const { return sealed_; }
  bool imageBaseKnown() const { return imageBaseKnown_; }
  uint64_t imageBase() const { return imageBase_; }
  bool entryPointKnown() const { return entryPointKnown_; }
  uint64_t entryPointAddress() const { return entryPointAddress_; }
  const std::vector<ExecutableSpan>& spans() const { return spans_; }

  bool validCoverage() const {
    if (!begun_ || !sealed_ || spans_.empty())
      return false;
    const uint64_t sourceEnd = sourceOffset_ + sourceLength_;
    uint64_t expected = sourceOffset_;
    for (const ExecutableSpan& span : spans_) {
      if (span.sourceLength == 0 || span.sourceOffset != expected ||
          span.sourceOffset > std::numeric_limits<uint64_t>::max() -
            span.sourceLength ||
          span.sourceOffset + span.sourceLength > sourceEnd)
        return false;
      if (span.kind == ExecutableSpanKind::CODE) {
        if (span.isa != isa_ || !span.hasVirtualAddress ||
            span.virtualAddress > std::numeric_limits<uint64_t>::max() -
              span.sourceLength)
          return false;
      } else if (span.isa != ExecutableIsa::NONE || span.hasVirtualAddress) {
        return false;
      }
      expected += span.sourceLength;
    }
    return expected == sourceEnd;
  }

private:
  ExecutableKind kind_ = ExecutableKind::PE_COFF;
  ExecutableIsa isa_ = ExecutableIsa::NONE;
  uint64_t sourceOffset_ = 0;
  uint64_t sourceLength_ = 0;
  bool imageBaseKnown_ = false;
  uint64_t imageBase_ = 0;
  bool entryPointKnown_ = false;
  uint64_t entryPointAddress_ = 0;
  bool begun_ = false;
  bool sealed_ = false;
  std::vector<ExecutableSpan> spans_;
};

namespace executable_layout_detail {

constexpr uint64_t kMaxFormatEntries = uint64_t{1} << 20;

inline bool addOk(uint64_t a, uint64_t b, uint64_t& result) {
  if (a > std::numeric_limits<uint64_t>::max() - b)
    return false;
  result = a + b;
  return true;
}

inline bool mulOk(uint64_t a, uint64_t b, uint64_t& result) {
  if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a)
    return false;
  result = a * b;
  return true;
}

class FilePositionRestorer {
public:
  explicit FilePositionRestorer(File* file)
    : file_(file), position_(file == nullptr ? 0 : file->curPos()) {}
  ~FilePositionRestorer() {
    if (file_ != nullptr)
      file_->setpos(position_);
  }

private:
  File* file_;
  uint64_t position_;
};

class Reader {
public:
  Reader(File* file, uint64_t sourceOffset, uint64_t sourceLength)
    : file_(file), sourceOffset_(sourceOffset), sourceLength_(sourceLength) {}

  uint64_t sourceOffset() const { return sourceOffset_; }
  uint64_t size() const { return sourceLength_; }

  bool read(uint64_t offset, uint8_t* destination, size_t bytes) const {
    if (file_ == nullptr || destination == nullptr ||
        offset > sourceLength_ || bytes > sourceLength_ - offset ||
        sourceOffset_ > std::numeric_limits<uint64_t>::max() - offset)
      return false;
    file_->setpos(sourceOffset_ + offset);
    return file_->blockRead(destination, bytes) == bytes;
  }

  bool u16(uint64_t offset, bool littleEndian, uint16_t& value) const {
    std::array<uint8_t, 2> bytes{};
    if (!read(offset, bytes.data(), bytes.size()))
      return false;
    value = littleEndian
      ? static_cast<uint16_t>(bytes[0] | (uint16_t{bytes[1]} << 8))
      : static_cast<uint16_t>((uint16_t{bytes[0]} << 8) | bytes[1]);
    return true;
  }

  bool u32(uint64_t offset, bool littleEndian, uint32_t& value) const {
    std::array<uint8_t, 4> bytes{};
    if (!read(offset, bytes.data(), bytes.size()))
      return false;
    if (littleEndian) {
      value = static_cast<uint32_t>(bytes[0]) |
              (static_cast<uint32_t>(bytes[1]) << 8) |
              (static_cast<uint32_t>(bytes[2]) << 16) |
              (static_cast<uint32_t>(bytes[3]) << 24);
    } else {
      value = (static_cast<uint32_t>(bytes[0]) << 24) |
              (static_cast<uint32_t>(bytes[1]) << 16) |
              (static_cast<uint32_t>(bytes[2]) << 8) |
              static_cast<uint32_t>(bytes[3]);
    }
    return true;
  }

  bool u64(uint64_t offset, bool littleEndian, uint64_t& value) const {
    std::array<uint8_t, 8> bytes{};
    if (!read(offset, bytes.data(), bytes.size()))
      return false;
    value = 0;
    if (littleEndian) {
      for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<uint64_t>(bytes[i]) << (i * 8);
    } else {
      for (unsigned i = 0; i < 8; ++i)
        value = (value << 8) | bytes[i];
    }
    return true;
  }

  bool range(uint64_t offset, uint64_t length) const {
    return offset <= sourceLength_ && length <= sourceLength_ - offset;
  }

private:
  File* file_;
  uint64_t sourceOffset_;
  uint64_t sourceLength_;
};

struct ByteRange {
  uint64_t offset = 0;
  uint64_t length = 0;
};

inline bool rangesDoNotOverlap(std::vector<ByteRange> ranges) {
  std::sort(ranges.begin(), ranges.end(),
            [](const ByteRange& a, const ByteRange& b) {
              if (a.offset != b.offset)
                return a.offset < b.offset;
              return a.length < b.length;
            });
  uint64_t previousEnd = 0;
  bool havePrevious = false;
  for (const ByteRange& range : ranges) {
    if (range.length == 0 ||
        range.offset > std::numeric_limits<uint64_t>::max() - range.length)
      return false;
    if (havePrevious && range.offset < previousEnd)
      return false;
    previousEnd = range.offset + range.length;
    havePrevious = true;
  }
  return true;
}

inline bool appendAbsoluteCode(const Reader& reader,
                               uint64_t relativeOffset,
                               uint64_t length,
                               ExecutableIsa isa,
                               uint64_t virtualAddress,
                               std::vector<ExecutableCodeRegion>& code) {
  if (length == 0)
    return true;
  if (!reader.range(relativeOffset, length) ||
      reader.sourceOffset() > std::numeric_limits<uint64_t>::max() -
        relativeOffset ||
      virtualAddress > std::numeric_limits<uint64_t>::max() - length)
    return false;
  ExecutableCodeRegion region;
  region.sourceOffset = reader.sourceOffset() + relativeOffset;
  region.sourceLength = length;
  region.isa = isa;
  region.virtualAddress = virtualAddress;
  code.push_back(region);
  return true;
}

inline ExecutableParseStatus parsePe(const Reader& reader,
                                     ExecutableLayout& result) {
  constexpr uint16_t kMachineI386 = 0x014cu;
  constexpr uint16_t kMachineAmd64 = 0x8664u;
  constexpr uint16_t kExecutableImage = 0x0002u;
  constexpr uint32_t kSectionCode = 0x00000020u;
  constexpr uint32_t kSectionExecute = 0x20000000u;
  constexpr uint64_t kSectionHeaderBytes = 40;

  uint32_t peOffset32 = 0;
  if (reader.size() < 64 || !reader.u32(0x3c, true, peOffset32))
    return ExecutableParseStatus::MALFORMED;
  const uint64_t peOffset = peOffset32;
  std::array<uint8_t, 4> signature{};
  if (!reader.read(peOffset, signature.data(), signature.size()) ||
      signature[0] != 'P' || signature[1] != 'E' ||
      signature[2] != 0 || signature[3] != 0)
    return ExecutableParseStatus::MALFORMED;

  uint64_t coffOffset = 0;
  if (!addOk(peOffset, 4, coffOffset) || !reader.range(coffOffset, 20))
    return ExecutableParseStatus::MALFORMED;
  uint16_t machine = 0, sectionCount = 0, optionalBytes = 0,
           characteristics = 0;
  if (!reader.u16(coffOffset, true, machine) ||
      !reader.u16(coffOffset + 2, true, sectionCount) ||
      !reader.u16(coffOffset + 16, true, optionalBytes) ||
      !reader.u16(coffOffset + 18, true, characteristics) ||
      sectionCount == 0 || sectionCount > kMaxFormatEntries ||
      (characteristics & kExecutableImage) == 0)
    return ExecutableParseStatus::MALFORMED;

  ExecutableIsa isa = ExecutableIsa::NONE;
  if (machine == kMachineI386)
    isa = ExecutableIsa::X86_32;
  else if (machine == kMachineAmd64)
    isa = ExecutableIsa::X86_64;
  else
    return ExecutableParseStatus::UNSUPPORTED;

  uint64_t optionalOffset = 0, sectionTableOffset = 0;
  if (!addOk(coffOffset, 20, optionalOffset) ||
      !addOk(optionalOffset, optionalBytes, sectionTableOffset) ||
      optionalBytes < 64 || !reader.range(optionalOffset, optionalBytes))
    return ExecutableParseStatus::MALFORMED;
  uint64_t sectionTableBytes = 0;
  if (!mulOk(sectionCount, kSectionHeaderBytes, sectionTableBytes) ||
      !reader.range(sectionTableOffset, sectionTableBytes))
    return ExecutableParseStatus::MALFORMED;

  uint16_t optionalMagic = 0;
  uint32_t entryRva = 0, sizeOfImage = 0, sizeOfHeaders = 0;
  uint64_t imageBase = 0;
  if (!reader.u16(optionalOffset, true, optionalMagic) ||
      !reader.u32(optionalOffset + 16, true, entryRva) ||
      !reader.u32(optionalOffset + 56, true, sizeOfImage) ||
      !reader.u32(optionalOffset + 60, true, sizeOfHeaders))
    return ExecutableParseStatus::MALFORMED;
  if (isa == ExecutableIsa::X86_32) {
    uint32_t base32 = 0;
    if (optionalMagic != 0x010bu ||
        !reader.u32(optionalOffset + 28, true, base32))
      return ExecutableParseStatus::MALFORMED;
    imageBase = base32;
  } else {
    if (optionalMagic != 0x020bu ||
        !reader.u64(optionalOffset + 24, true, imageBase))
      return ExecutableParseStatus::MALFORMED;
  }
  uint64_t sectionTableEnd = 0;
  if (!addOk(sectionTableOffset, sectionTableBytes, sectionTableEnd) ||
      sizeOfHeaders < sectionTableEnd || sizeOfHeaders > reader.size() ||
      sizeOfImage == 0)
    return ExecutableParseStatus::MALFORMED;

  std::vector<ByteRange> rawRanges;
  std::vector<ExecutableCodeRegion> code;
  rawRanges.reserve(sectionCount);
  code.reserve(sectionCount);
  for (uint64_t i = 0; i < sectionCount; ++i) {
    const uint64_t section = sectionTableOffset + i * kSectionHeaderBytes;
    uint32_t virtualSize = 0, virtualAddress = 0, rawSize = 0,
             rawOffset = 0, flags = 0;
    if (!reader.u32(section + 8, true, virtualSize) ||
        !reader.u32(section + 12, true, virtualAddress) ||
        !reader.u32(section + 16, true, rawSize) ||
        !reader.u32(section + 20, true, rawOffset) ||
        !reader.u32(section + 36, true, flags))
      return ExecutableParseStatus::MALFORMED;
    if (rawSize != 0) {
      if (rawOffset < sizeOfHeaders || !reader.range(rawOffset, rawSize))
        return ExecutableParseStatus::MALFORMED;
      rawRanges.push_back({rawOffset, rawSize});
    }
    uint64_t virtualEnd = 0;
    if (!addOk(virtualAddress, virtualSize, virtualEnd) ||
        virtualEnd > sizeOfImage)
      return ExecutableParseStatus::MALFORMED;

    if ((flags & kSectionCode) != 0 && (flags & kSectionExecute) != 0 &&
        rawSize != 0 && virtualSize != 0) {
      const uint64_t codeBytes = std::min<uint64_t>(rawSize, virtualSize);
      uint64_t codeAddress = 0;
      if (!addOk(imageBase, virtualAddress, codeAddress) ||
          !appendAbsoluteCode(reader, rawOffset, codeBytes, isa,
                              codeAddress, code))
        return ExecutableParseStatus::MALFORMED;
    }
  }
  if (!rangesDoNotOverlap(rawRanges))
    return ExecutableParseStatus::MALFORMED;

  ExecutableLayout layout;
  if (!layout.begin(ExecutableKind::PE_COFF, isa, reader.sourceOffset(),
                    reader.size()) ||
      !layout.setImageBase(imageBase))
    return ExecutableParseStatus::MALFORMED;
  if (entryRva != 0) {
    uint64_t entry = 0;
    if (!addOk(imageBase, entryRva, entry) || entryRva >= sizeOfImage ||
        !layout.setEntryPoint(entry))
      return ExecutableParseStatus::MALFORMED;
  }
  if (!layout.seal(code))
    return ExecutableParseStatus::MALFORMED;
  result = layout;
  return ExecutableParseStatus::SUCCESS;
}

struct ElfHeaderInfo {
  bool is64 = false;
  bool littleEndian = true;
  ExecutableIsa isa = ExecutableIsa::NONE;
  uint64_t entry = 0;
  uint64_t programOffset = 0;
  uint64_t sectionOffset = 0;
  uint16_t programEntryBytes = 0;
  uint16_t programCountField = 0;
  uint16_t sectionEntryBytes = 0;
  uint16_t sectionCountField = 0;
  uint16_t stringIndexField = 0;
};

inline bool readElfHeader(const Reader& reader, ElfHeaderInfo& info,
                          ExecutableParseStatus& failure) {
  std::array<uint8_t, 16> ident{};
  if (!reader.read(0, ident.data(), ident.size())) {
    failure = ExecutableParseStatus::MALFORMED;
    return false;
  }
  if (ident[4] != 1 && ident[4] != 2) {
    failure = ExecutableParseStatus::UNSUPPORTED;
    return false;
  }
  if (ident[5] != 1 && ident[5] != 2) {
    failure = ExecutableParseStatus::UNSUPPORTED;
    return false;
  }
  if (ident[6] != 1) {
    failure = ExecutableParseStatus::MALFORMED;
    return false;
  }
  info.is64 = ident[4] == 2;
  info.littleEndian = ident[5] == 1;
  const uint64_t headerBytes = info.is64 ? 64 : 52;
  if (reader.size() < headerBytes) {
    failure = ExecutableParseStatus::MALFORMED;
    return false;
  }
  uint16_t type = 0, machine = 0, headerSize = 0;
  uint32_t version = 0;
  if (!reader.u16(16, info.littleEndian, type) ||
      !reader.u16(18, info.littleEndian, machine) ||
      !reader.u32(20, info.littleEndian, version) || version != 1 ||
      (type != 2 && type != 3)) {
    failure = ExecutableParseStatus::MALFORMED;
    return false;
  }
  if (machine == 3 && !info.is64)
    info.isa = ExecutableIsa::X86_32;
  else if (machine == 62 && info.is64)
    info.isa = ExecutableIsa::X86_64;
  else {
    failure = ExecutableParseStatus::UNSUPPORTED;
    return false;
  }

  if (info.is64) {
    if (!reader.u64(24, info.littleEndian, info.entry) ||
        !reader.u64(32, info.littleEndian, info.programOffset) ||
        !reader.u64(40, info.littleEndian, info.sectionOffset) ||
        !reader.u16(52, info.littleEndian, headerSize) ||
        !reader.u16(54, info.littleEndian, info.programEntryBytes) ||
        !reader.u16(56, info.littleEndian, info.programCountField) ||
        !reader.u16(58, info.littleEndian, info.sectionEntryBytes) ||
        !reader.u16(60, info.littleEndian, info.sectionCountField) ||
        !reader.u16(62, info.littleEndian, info.stringIndexField)) {
      failure = ExecutableParseStatus::MALFORMED;
      return false;
    }
  } else {
    uint32_t entry32 = 0, program32 = 0, section32 = 0;
    if (!reader.u32(24, info.littleEndian, entry32) ||
        !reader.u32(28, info.littleEndian, program32) ||
        !reader.u32(32, info.littleEndian, section32) ||
        !reader.u16(40, info.littleEndian, headerSize) ||
        !reader.u16(42, info.littleEndian, info.programEntryBytes) ||
        !reader.u16(44, info.littleEndian, info.programCountField) ||
        !reader.u16(46, info.littleEndian, info.sectionEntryBytes) ||
        !reader.u16(48, info.littleEndian, info.sectionCountField) ||
        !reader.u16(50, info.littleEndian, info.stringIndexField)) {
      failure = ExecutableParseStatus::MALFORMED;
      return false;
    }
    info.entry = entry32;
    info.programOffset = program32;
    info.sectionOffset = section32;
  }
  if (headerSize != headerBytes) {
    failure = ExecutableParseStatus::MALFORMED;
    return false;
  }
  return true;
}

inline ExecutableParseStatus parseElf(const Reader& reader,
                                      ExecutableLayout& result) {
  constexpr uint16_t kPnXnum = 0xffffu;
  constexpr uint16_t kShnXindex = 0xffffu;
  constexpr uint64_t kShfAlloc = 0x2u;
  constexpr uint64_t kShfExecInstr = 0x4u;
  constexpr uint32_t kShtNoBits = 8u;
  constexpr uint32_t kPtLoad = 1u;
  constexpr uint32_t kPfExecute = 1u;

  ElfHeaderInfo header;
  ExecutableParseStatus failure = ExecutableParseStatus::MALFORMED;
  if (!readElfHeader(reader, header, failure))
    return failure;
  const uint64_t expectedProgramBytes = header.is64 ? 56 : 32;
  const uint64_t expectedSectionBytes = header.is64 ? 64 : 40;
  const bool sectionsAbsent =
    header.sectionOffset == 0 && header.sectionCountField == 0;

  uint64_t sectionCount = header.sectionCountField;
  uint64_t programCount = header.programCountField;
  uint64_t stringIndex = header.stringIndexField;
  uint64_t sectionZeroSize = 0;
  uint32_t sectionZeroLink = 0, sectionZeroInfo = 0;
  const bool needsSectionZero =
    (!sectionsAbsent && header.sectionCountField == 0) ||
    header.programCountField == kPnXnum ||
    header.stringIndexField == kShnXindex;
  if (needsSectionZero) {
    if (header.sectionOffset == 0 ||
        header.sectionEntryBytes != expectedSectionBytes ||
        !reader.range(header.sectionOffset, expectedSectionBytes))
      return ExecutableParseStatus::MALFORMED;
    uint32_t sectionZeroType = 0;
    if (!reader.u32(header.sectionOffset + 4, header.littleEndian,
                    sectionZeroType) || sectionZeroType != 0)
      return ExecutableParseStatus::MALFORMED;
    if (header.is64) {
      if (!reader.u64(header.sectionOffset + 32, header.littleEndian,
                      sectionZeroSize) ||
          !reader.u32(header.sectionOffset + 40, header.littleEndian,
                      sectionZeroLink) ||
          !reader.u32(header.sectionOffset + 44, header.littleEndian,
                      sectionZeroInfo))
        return ExecutableParseStatus::MALFORMED;
    } else {
      uint32_t size32 = 0;
      if (!reader.u32(header.sectionOffset + 20, header.littleEndian, size32) ||
          !reader.u32(header.sectionOffset + 24, header.littleEndian,
                      sectionZeroLink) ||
          !reader.u32(header.sectionOffset + 28, header.littleEndian,
                      sectionZeroInfo))
        return ExecutableParseStatus::MALFORMED;
      sectionZeroSize = size32;
    }
  }
  if (!sectionsAbsent && header.sectionCountField == 0)
    sectionCount = sectionZeroSize;
  if (header.programCountField == kPnXnum)
    programCount = sectionZeroInfo;
  if (header.stringIndexField == kShnXindex)
    stringIndex = sectionZeroLink;

  if (sectionCount > kMaxFormatEntries || programCount == 0 ||
      programCount > kMaxFormatEntries)
    return ExecutableParseStatus::MALFORMED;
  if (sectionsAbsent) {
    if ((header.sectionEntryBytes != 0 &&
         header.sectionEntryBytes != expectedSectionBytes) ||
        header.stringIndexField != 0)
      return ExecutableParseStatus::MALFORMED;
  } else {
    uint64_t tableBytes = 0;
    if (header.sectionOffset == 0 || sectionCount == 0 ||
        header.sectionEntryBytes != expectedSectionBytes ||
        !mulOk(sectionCount, expectedSectionBytes, tableBytes) ||
        !reader.range(header.sectionOffset, tableBytes) ||
        (stringIndex != 0 && stringIndex >= sectionCount))
      return ExecutableParseStatus::MALFORMED;
  }
  uint64_t programTableBytes = 0;
  if (header.programOffset == 0 ||
      header.programEntryBytes != expectedProgramBytes ||
      !mulOk(programCount, expectedProgramBytes, programTableBytes) ||
      !reader.range(header.programOffset, programTableBytes))
    return ExecutableParseStatus::MALFORMED;

  std::vector<ExecutableCodeRegion> code;
  std::vector<ByteRange> executableLoads;
  // Program headers are validated even when section metadata is available.
  // They become code candidates only for the explicitly sectionless fallback.
  for (uint64_t i = 0; i < programCount; ++i) {
    const uint64_t program = header.programOffset + i * expectedProgramBytes;
    uint32_t type = 0, flags = 0;
    uint64_t offset = 0, virtualAddress = 0, fileBytes = 0,
             memoryBytes = 0;
    if (!reader.u32(program, header.littleEndian, type))
      return ExecutableParseStatus::MALFORMED;
    if (header.is64) {
      if (!reader.u32(program + 4, header.littleEndian, flags) ||
          !reader.u64(program + 8, header.littleEndian, offset) ||
          !reader.u64(program + 16, header.littleEndian, virtualAddress) ||
          !reader.u64(program + 32, header.littleEndian, fileBytes) ||
          !reader.u64(program + 40, header.littleEndian, memoryBytes))
        return ExecutableParseStatus::MALFORMED;
    } else {
      uint32_t offset32 = 0, address32 = 0, fileBytes32 = 0,
               memoryBytes32 = 0;
      if (!reader.u32(program + 4, header.littleEndian, offset32) ||
          !reader.u32(program + 8, header.littleEndian, address32) ||
          !reader.u32(program + 16, header.littleEndian, fileBytes32) ||
          !reader.u32(program + 20, header.littleEndian, memoryBytes32) ||
          !reader.u32(program + 24, header.littleEndian, flags))
        return ExecutableParseStatus::MALFORMED;
      offset = offset32;
      virtualAddress = address32;
      fileBytes = fileBytes32;
      memoryBytes = memoryBytes32;
    }
    uint64_t memoryEnd = 0;
    if ((fileBytes != 0 && !reader.range(offset, fileBytes)) ||
        (type == kPtLoad &&
         (fileBytes > memoryBytes ||
          !addOk(virtualAddress, memoryBytes, memoryEnd))))
      return ExecutableParseStatus::MALFORMED;
    if (sectionsAbsent && type == kPtLoad &&
        (flags & kPfExecute) != 0 && fileBytes != 0) {
      executableLoads.push_back({offset, fileBytes});
      if (!appendAbsoluteCode(reader, offset, fileBytes, header.isa,
                              virtualAddress, code))
        return ExecutableParseStatus::MALFORMED;
    }
  }
  if (!rangesDoNotOverlap(executableLoads))
    return ExecutableParseStatus::MALFORMED;

  if (!sectionsAbsent) {
    code.reserve(static_cast<size_t>(std::min<uint64_t>(sectionCount, 4096)));
    for (uint64_t i = 0; i < sectionCount; ++i) {
      const uint64_t section = header.sectionOffset + i * expectedSectionBytes;
      uint32_t type = 0;
      uint64_t flags = 0, address = 0, offset = 0, size = 0;
      if (!reader.u32(section + 4, header.littleEndian, type))
        return ExecutableParseStatus::MALFORMED;
      if (header.is64) {
        if (!reader.u64(section + 8, header.littleEndian, flags) ||
            !reader.u64(section + 16, header.littleEndian, address) ||
            !reader.u64(section + 24, header.littleEndian, offset) ||
            !reader.u64(section + 32, header.littleEndian, size))
          return ExecutableParseStatus::MALFORMED;
      } else {
        uint32_t flags32 = 0, address32 = 0, offset32 = 0, size32 = 0;
        if (!reader.u32(section + 8, header.littleEndian, flags32) ||
            !reader.u32(section + 12, header.littleEndian, address32) ||
            !reader.u32(section + 16, header.littleEndian, offset32) ||
            !reader.u32(section + 20, header.littleEndian, size32))
          return ExecutableParseStatus::MALFORMED;
        flags = flags32;
        address = address32;
        offset = offset32;
        size = size32;
      }
      if (type != kShtNoBits && size != 0 && !reader.range(offset, size))
        return ExecutableParseStatus::MALFORMED;
      if (type != kShtNoBits && (flags & kShfAlloc) != 0 &&
          (flags & kShfExecInstr) != 0 &&
          !appendAbsoluteCode(reader, offset, size, header.isa, address, code))
        return ExecutableParseStatus::MALFORMED;
    }
  }

  ExecutableLayout layout;
  if (!layout.begin(ExecutableKind::ELF, header.isa, reader.sourceOffset(),
                    reader.size()))
    return ExecutableParseStatus::MALFORMED;
  if (header.entry != 0 && !layout.setEntryPoint(header.entry))
    return ExecutableParseStatus::MALFORMED;
  if (!layout.seal(code))
    return ExecutableParseStatus::MALFORMED;
  result = layout;
  return ExecutableParseStatus::SUCCESS;
}

inline bool fixedNameEquals(const std::array<uint8_t, 16>& name,
                            const char* expected) {
  const size_t length = std::strlen(expected);
  if (length > name.size())
    return false;
  for (size_t i = 0; i < name.size(); ++i) {
    const uint8_t wanted = i < length
      ? static_cast<uint8_t>(expected[i]) : uint8_t{0};
    if (name[i] != wanted)
      return false;
  }
  return true;
}

struct MachSegmentMap {
  uint64_t fileOffset = 0;
  uint64_t fileLength = 0;
  uint64_t virtualAddress = 0;
  uint64_t virtualLength = 0;
  bool executableText = false;
};

inline ExecutableParseStatus parseMachO(const Reader& reader,
                                        bool is64, bool littleEndian,
                                        ExecutableLayout& result) {
  constexpr uint32_t kCpuX86 = 7u;
  constexpr uint32_t kCpuX86_64 = 0x01000007u;
  constexpr uint32_t kLcSegment = 0x1u;
  constexpr uint32_t kLcSegment64 = 0x19u;
  constexpr uint32_t kLcMain = 0x80000028u;
  constexpr uint32_t kVmProtExecute = 0x4u;
  constexpr uint32_t kPureInstructions = 0x80000000u;
  constexpr uint32_t kSomeInstructions = 0x00000400u;
  constexpr uint32_t kSectionTypeMask = 0xffu;
  constexpr uint32_t kZeroFill = 1u;
  constexpr uint32_t kGbZeroFill = 12u;
  constexpr uint32_t kThreadLocalZeroFill = 18u;

  const uint64_t headerBytes = is64 ? 32 : 28;
  if (reader.size() < headerBytes)
    return ExecutableParseStatus::MALFORMED;
  uint32_t cpuType = 0, fileType = 0, commandCount = 0,
           commandBytes32 = 0;
  if (!reader.u32(4, littleEndian, cpuType) ||
      !reader.u32(12, littleEndian, fileType) ||
      !reader.u32(16, littleEndian, commandCount) ||
      !reader.u32(20, littleEndian, commandBytes32))
    return ExecutableParseStatus::MALFORMED;
  ExecutableIsa isa = ExecutableIsa::NONE;
  if (cpuType == kCpuX86 && !is64)
    isa = ExecutableIsa::X86_32;
  else if (cpuType == kCpuX86_64 && is64)
    isa = ExecutableIsa::X86_64;
  else
    return ExecutableParseStatus::UNSUPPORTED;
  // Restrict routing to linked, code-bearing images. Relocatable objects and
  // core files may describe sections whose final instruction addresses are not
  // yet established.
  if (fileType != 2 && fileType != 6 && fileType != 7 &&
      fileType != 8 && fileType != 11)
    return ExecutableParseStatus::UNSUPPORTED;
  if (commandCount > kMaxFormatEntries ||
      commandCount > commandBytes32 / 8 ||
      !reader.range(headerBytes, commandBytes32))
    return ExecutableParseStatus::MALFORMED;

  const uint64_t commandEnd = headerBytes + commandBytes32;
  uint64_t commandOffset = headerBytes;
  bool mainEntrySeen = false;
  bool segmentSeen = false;
  uint64_t mainEntryFileOffset = 0;
  std::vector<ByteRange> segmentFileRanges;
  std::vector<MachSegmentMap> segmentMaps;
  std::vector<ExecutableCodeRegion> code;
  for (uint64_t i = 0; i < commandCount; ++i) {
    uint32_t command = 0, commandSize = 0;
    if (!reader.u32(commandOffset, littleEndian, command) ||
        !reader.u32(commandOffset + 4, littleEndian, commandSize) ||
        commandSize < 8 || commandSize > commandEnd - commandOffset)
      return ExecutableParseStatus::MALFORMED;

    const bool segment32 = command == kLcSegment;
    const bool segment64 = command == kLcSegment64;
    if (segment32 || segment64) {
      segmentSeen = true;
      if (segment32 == is64)
        return ExecutableParseStatus::MALFORMED;
      const uint64_t segmentHeaderBytes = is64 ? 72 : 56;
      const uint64_t sectionBytes = is64 ? 80 : 68;
      if (commandSize < segmentHeaderBytes)
        return ExecutableParseStatus::MALFORMED;
      std::array<uint8_t, 16> segmentName{};
      if (!reader.read(commandOffset + 8, segmentName.data(),
                       segmentName.size()))
        return ExecutableParseStatus::MALFORMED;
      uint64_t vmAddress = 0, vmLength = 0, fileOffset = 0, fileLength = 0;
      uint32_t initialProtection = 0, sectionCount = 0;
      if (is64) {
        if (!reader.u64(commandOffset + 24, littleEndian, vmAddress) ||
            !reader.u64(commandOffset + 32, littleEndian, vmLength) ||
            !reader.u64(commandOffset + 40, littleEndian, fileOffset) ||
            !reader.u64(commandOffset + 48, littleEndian, fileLength) ||
            !reader.u32(commandOffset + 60, littleEndian,
                        initialProtection) ||
            !reader.u32(commandOffset + 64, littleEndian, sectionCount))
          return ExecutableParseStatus::MALFORMED;
      } else {
        uint32_t vmAddress32 = 0, vmLength32 = 0, fileOffset32 = 0,
                 fileLength32 = 0;
        if (!reader.u32(commandOffset + 24, littleEndian, vmAddress32) ||
            !reader.u32(commandOffset + 28, littleEndian, vmLength32) ||
            !reader.u32(commandOffset + 32, littleEndian, fileOffset32) ||
            !reader.u32(commandOffset + 36, littleEndian, fileLength32) ||
            !reader.u32(commandOffset + 44, littleEndian,
                        initialProtection) ||
            !reader.u32(commandOffset + 48, littleEndian, sectionCount))
          return ExecutableParseStatus::MALFORMED;
        vmAddress = vmAddress32;
        vmLength = vmLength32;
        fileOffset = fileOffset32;
        fileLength = fileLength32;
      }
      uint64_t allSectionsBytes = 0, exactCommandBytes = 0;
      if (sectionCount > kMaxFormatEntries ||
          !mulOk(sectionCount, sectionBytes, allSectionsBytes) ||
          !addOk(segmentHeaderBytes, allSectionsBytes, exactCommandBytes) ||
          commandSize != exactCommandBytes ||
          (fileLength != 0 && !reader.range(fileOffset, fileLength)))
        return ExecutableParseStatus::MALFORMED;
      uint64_t vmEnd = 0;
      if (!addOk(vmAddress, vmLength, vmEnd))
        return ExecutableParseStatus::MALFORMED;
      if (fileLength != 0)
        segmentFileRanges.push_back({fileOffset, fileLength});

      MachSegmentMap map;
      map.fileOffset = fileOffset;
      map.fileLength = fileLength;
      map.virtualAddress = vmAddress;
      map.virtualLength = vmLength;
      map.executableText = fixedNameEquals(segmentName, "__TEXT") &&
                           (initialProtection & kVmProtExecute) != 0;
      segmentMaps.push_back(map);

      for (uint64_t sectionIndex = 0; sectionIndex < sectionCount;
           ++sectionIndex) {
        const uint64_t section = commandOffset + segmentHeaderBytes +
                                 sectionIndex * sectionBytes;
        std::array<uint8_t, 16> sectionSegmentName{};
        if (!reader.read(section + 16, sectionSegmentName.data(),
                         sectionSegmentName.size()))
          return ExecutableParseStatus::MALFORMED;
        uint64_t address = 0, size = 0;
        uint32_t offset = 0, flags = 0;
        if (is64) {
          if (!reader.u64(section + 32, littleEndian, address) ||
              !reader.u64(section + 40, littleEndian, size) ||
              !reader.u32(section + 48, littleEndian, offset) ||
              !reader.u32(section + 64, littleEndian, flags))
            return ExecutableParseStatus::MALFORMED;
        } else {
          uint32_t address32 = 0, size32 = 0;
          if (!reader.u32(section + 32, littleEndian, address32) ||
              !reader.u32(section + 36, littleEndian, size32) ||
              !reader.u32(section + 40, littleEndian, offset) ||
              !reader.u32(section + 56, littleEndian, flags))
            return ExecutableParseStatus::MALFORMED;
          address = address32;
          size = size32;
        }
        const uint32_t sectionType = flags & kSectionTypeMask;
        const bool zeroFill = sectionType == kZeroFill ||
                              sectionType == kGbZeroFill ||
                              sectionType == kThreadLocalZeroFill;
        if (!zeroFill && size != 0) {
          uint64_t sectionEnd = 0, fileEnd = 0, addressEnd = 0;
          if (!addOk(offset, size, sectionEnd) ||
              !addOk(fileOffset, fileLength, fileEnd) ||
              !addOk(address, size, addressEnd) ||
              sectionEnd > fileEnd || offset < fileOffset ||
              address < vmAddress || addressEnd > vmEnd ||
              address - vmAddress != offset - fileOffset ||
              !reader.range(offset, size))
            return ExecutableParseStatus::MALFORMED;
        }
        const bool instructions =
          (flags & (kPureInstructions | kSomeInstructions)) != 0;
        if (!zeroFill && size != 0 && map.executableText && instructions &&
            fixedNameEquals(sectionSegmentName, "__TEXT") &&
            !appendAbsoluteCode(reader, offset, size, isa, address, code))
          return ExecutableParseStatus::MALFORMED;
      }
    } else if (command == kLcMain) {
      uint64_t entryOffset = 0;
      if (commandSize != 24 || mainEntrySeen ||
          !reader.u64(commandOffset + 8, littleEndian, entryOffset) ||
          entryOffset >= reader.size())
        return ExecutableParseStatus::MALFORMED;
      mainEntrySeen = true;
      mainEntryFileOffset = entryOffset;
    }
    commandOffset += commandSize;
  }
  if (commandOffset != commandEnd || !segmentSeen ||
      !rangesDoNotOverlap(segmentFileRanges))
    return ExecutableParseStatus::MALFORMED;

  ExecutableLayout layout;
  if (!layout.begin(ExecutableKind::MACH_O, isa, reader.sourceOffset(),
                    reader.size()))
    return ExecutableParseStatus::MALFORMED;
  uint64_t minimumTextBase = std::numeric_limits<uint64_t>::max();
  for (const MachSegmentMap& map : segmentMaps) {
    if (map.executableText)
      minimumTextBase = std::min(minimumTextBase, map.virtualAddress);
  }
  if (minimumTextBase != std::numeric_limits<uint64_t>::max() &&
      !layout.setImageBase(minimumTextBase))
    return ExecutableParseStatus::MALFORMED;
  if (mainEntrySeen) {
    bool mapped = false;
    uint64_t entryAddress = 0;
    for (const MachSegmentMap& map : segmentMaps) {
      if (map.fileLength != 0 && mainEntryFileOffset >= map.fileOffset &&
          mainEntryFileOffset - map.fileOffset < map.fileLength) {
        if (mapped || !addOk(map.virtualAddress,
                             mainEntryFileOffset - map.fileOffset,
                             entryAddress))
          return ExecutableParseStatus::MALFORMED;
        mapped = true;
      }
    }
    if (!mapped || !layout.setEntryPoint(entryAddress))
      return ExecutableParseStatus::MALFORMED;
  }
  if (!layout.seal(code))
    return ExecutableParseStatus::MALFORMED;
  result = layout;
  return ExecutableParseStatus::SUCCESS;
}

} // namespace executable_layout_detail

// Parse one complete source interval.  The input cursor is restored on every
// result, including malformed and unsupported files.  A recognized malformed
// image is never downgraded to a guessed layout.
inline ExecutableParseStatus parseExecutableLayout(
    File* source, uint64_t sourceOffset, uint64_t sourceLength,
    ExecutableLayout& result) {
  using namespace executable_layout_detail;
  if (source == nullptr || sourceLength < 4 ||
      sourceOffset > std::numeric_limits<uint64_t>::max() - sourceLength)
    return ExecutableParseStatus::NOT_EXECUTABLE;
  FilePositionRestorer restore(source);
  Reader reader(source, sourceOffset, sourceLength);
  std::array<uint8_t, 16> magic{};
  const size_t magicBytes = static_cast<size_t>(
    std::min<uint64_t>(sourceLength, magic.size()));
  if (!reader.read(0, magic.data(), magicBytes))
    return ExecutableParseStatus::MALFORMED;

  if (magic[0] == 'M' && magic[1] == 'Z')
    return parsePe(reader, result);
  if (magic[0] == 0x7f && magic[1] == 'E' && magic[2] == 'L' &&
      magic[3] == 'F')
    return parseElf(reader, result);

  const bool mach32Little = magic[0] == 0xce && magic[1] == 0xfa &&
                            magic[2] == 0xed && magic[3] == 0xfe;
  const bool mach64Little = magic[0] == 0xcf && magic[1] == 0xfa &&
                            magic[2] == 0xed && magic[3] == 0xfe;
  const bool mach32Big = magic[0] == 0xfe && magic[1] == 0xed &&
                         magic[2] == 0xfa && magic[3] == 0xce;
  const bool mach64Big = magic[0] == 0xfe && magic[1] == 0xed &&
                         magic[2] == 0xfa && magic[3] == 0xcf;
  if (mach32Little || mach64Little || mach32Big || mach64Big)
    return parseMachO(reader, mach64Little || mach64Big,
                      mach32Little || mach64Little, result);

  // FAT_MAGIC/FAT_CIGAM and their 64-bit variants are deliberately not
  // traversed: routing a universal binary would require a separately frozen
  // slice/reassembly contract.
  const bool fat =
    (magic[0] == 0xca && magic[1] == 0xfe && magic[2] == 0xba &&
     (magic[3] == 0xbe || magic[3] == 0xbf)) ||
    ((magic[0] == 0xbe || magic[0] == 0xbf) && magic[1] == 0xba &&
     magic[2] == 0xfe && magic[3] == 0xca);
  return fat ? ExecutableParseStatus::UNSUPPORTED
             : ExecutableParseStatus::NOT_EXECUTABLE;
}

inline bool detectExecutableLayout(File* source, uint64_t sourceOffset,
                                   uint64_t sourceLength,
                                   ExecutableLayout& result) {
  return parseExecutableLayout(source, sourceOffset, sourceLength, result) ==
         ExecutableParseStatus::SUCCESS;
}

} // namespace routed
