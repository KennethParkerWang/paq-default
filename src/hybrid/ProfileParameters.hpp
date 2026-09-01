#pragma once

#include "../Utils.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace routed {

constexpr uint8_t kMaximumNumericRank = 8;
constexpr uint32_t kMaximumEmbeddedSchemaBytes = 1024u * 1024u;

class CanonicalWriter {
public:
  void putByte(uint8_t value) { bytes_.push_back(value); }

  void putBool(bool value) { putByte(value ? 1 : 0); }

  void putUleb128(uint64_t value) {
    do {
      uint8_t byte = static_cast<uint8_t>(value & 0x7fu);
      value >>= 7;
      if (value != 0)
        byte |= 0x80u;
      bytes_.push_back(byte);
    } while (value != 0);
  }

  void putBlob(const std::vector<uint8_t>& value) {
    putUleb128(value.size());
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  const std::vector<uint8_t>& bytes() const { return bytes_; }
  std::vector<uint8_t> take() { return std::move(bytes_); }

private:
  std::vector<uint8_t> bytes_;
};

class CanonicalReader {
public:
  explicit CanonicalReader(const std::vector<uint8_t>& bytes)
    : data_(bytes.data()), size_(bytes.size()) {}

  CanonicalReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool readByte(uint8_t& value) {
    if (position_ >= size_)
      return false;
    value = data_[position_++];
    return true;
  }

  bool readBool(bool& value) {
    uint8_t byte = 0;
    if (!readByte(byte) || byte > 1)
      return false;
    value = byte != 0;
    return true;
  }

  bool readUleb128(uint64_t& value) {
    value = 0;
    unsigned shift = 0;
    for (unsigned index = 0; index < 10; ++index) {
      uint8_t byte = 0;
      if (!readByte(byte))
        return false;
      const uint8_t payload = static_cast<uint8_t>(byte & 0x7fu);
      if (shift == 63 && payload > 1)
        return false;
      value |= static_cast<uint64_t>(payload) << shift;
      if ((byte & 0x80u) == 0) {
        if (index != 0 && payload == 0)
          return false; // Redundant ULEB128 groups are not canonical.
        return true;
      }
      shift += 7;
    }
    return false;
  }

  bool readBlob(std::vector<uint8_t>& value, uint64_t maximumSize) {
    uint64_t length = 0;
    if (!readUleb128(length) || length > maximumSize ||
        length > size_ - position_)
      return false;
    if (length == 0) {
      value.clear();
      return true;
    }
    value.assign(data_ + position_, data_ + position_ + static_cast<size_t>(length));
    position_ += static_cast<size_t>(length);
    return true;
  }

  bool atEnd() const { return position_ == size_; }
  size_t remaining() const { return size_ - position_; }

private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  size_t position_ = 0;
};

enum class TextEncoding : uint8_t {
  BYTE_OR_UNKNOWN = 0,
  UTF8 = 1,
  UTF16_LE = 2,
  UTF16_BE = 3,
  UTF32_LE = 4,
  UTF32_BE = 5
};

enum class TextFlavor : uint8_t {
  GENERIC = 0,
  NATURAL_LANGUAGE = 1,
  SOURCE_CODE = 2,
  MARKUP = 3,
  STRUCTURED = 4,
  LOG = 5
};

enum class NewlineKind : uint8_t {
  UNKNOWN_OR_MIXED = 0,
  LF = 1,
  CRLF = 2,
  CR = 3
};

enum class ScalarKind : uint8_t {
  UNSIGNED_INTEGER = 0,
  SIGNED_INTEGER = 1,
  IEEE754_FLOAT = 2
};

enum class Endian : uint8_t {
  LITTLE = 0,
  BIG = 1,
  NOT_APPLICABLE = 2
};

enum class LayoutKind : uint8_t {
  ROW_MAJOR = 0,
  COLUMN_MAJOR = 1,
  PLANAR = 2,
  INTERLEAVED = 3
};

enum class PixelLayout : uint8_t {
  GRAY = 0,
  RGB = 1,
  RGBA = 2,
  BGR = 3,
  BGRA = 4,
  BAYER = 5,
  YUV = 6,
  UNKNOWN_TYPED = 7
};

enum class BayerPattern : uint8_t {
  NONE = 0,
  RGGB = 1,
  BGGR = 2,
  GRBG = 3,
  GBRG = 4
};

enum class SampleKind : uint8_t {
  UNSIGNED_PCM = 0,
  SIGNED_PCM = 1,
  IEEE754_PCM = 2
};

enum class Interleave : uint8_t {
  INTERLEAVED = 0,
  PLANAR = 1
};

enum class IsaKind : uint8_t {
  X86 = 0,
  X86_64 = 1,
  ARM32 = 2,
  ARM64 = 3,
  RISCV32 = 4,
  RISCV64 = 5,
  DEC_ALPHA = 6
};

struct TextParams {
  TextEncoding encoding = TextEncoding::BYTE_OR_UNKNOWN;
  TextFlavor flavor = TextFlavor::GENERIC;
  NewlineKind newline = NewlineKind::UNKNOWN_OR_MIXED;
  bool hasBom = false;
  bool preserveInvalidSequences = true;
  uint8_t delimiter = 0;
  uint8_t quote = 0;
  uint8_t escape = 0;
  uint32_t fixedColumnSchemaId = 0;
};

struct RecordParams {
  uint32_t stride = 0;
  uint32_t alignment = 1;
  uint32_t phase = 0;
  uint64_t recordCountHint = 0;
  uint32_t schemaId = 0;
  std::vector<uint8_t> fieldPhaseMask;
  std::vector<uint8_t> embeddedSchema;
};

struct NumericParams {
  ScalarKind scalarKind = ScalarKind::UNSIGNED_INTEGER;
  uint8_t widthBytes = 1;
  Endian endian = Endian::NOT_APPLICABLE;
  uint8_t rank = 1;
  uint8_t channelCount = 1;
  LayoutKind layout = LayoutKind::ROW_MAJOR;
  std::array<uint64_t, kMaximumNumericRank> dimensions{};
};

struct RasterParams {
  uint64_t width = 0;
  uint64_t height = 0;
  uint32_t rowStride = 0;
  uint8_t bitsPerSample = 0;
  uint8_t channels = 0;
  PixelLayout pixelLayout = PixelLayout::UNKNOWN_TYPED;
  BayerPattern bayerPattern = BayerPattern::NONE;
};

struct AudioParams {
  SampleKind sampleKind = SampleKind::SIGNED_PCM;
  uint8_t bitsPerSample = 0;
  uint16_t channels = 0;
  uint32_t sampleRate = 0;
  Endian endian = Endian::LITTLE;
  Interleave interleave = Interleave::INTERLEAVED;
};

struct CodeParams {
  IsaKind isa = IsaKind::X86_64;
  uint8_t addressBits = 64;
  uint64_t virtualBase = 0;
  uint64_t sourceOffset = 0;
  uint32_t sectionFlags = 0;
};

inline bool checkedMultiply(uint64_t a, uint64_t b, uint64_t& result) {
  if (a != 0 && b > std::numeric_limits<uint64_t>::max() / a)
    return false;
  result = a * b;
  return true;
}

inline bool validNumericParams(const NumericParams& value) {
  if (static_cast<uint8_t>(value.scalarKind) >
        static_cast<uint8_t>(ScalarKind::IEEE754_FLOAT) ||
      static_cast<uint8_t>(value.endian) >
        static_cast<uint8_t>(Endian::NOT_APPLICABLE) ||
      static_cast<uint8_t>(value.layout) >
        static_cast<uint8_t>(LayoutKind::INTERLEAVED))
    return false;
  if (value.widthBytes != 1 && value.widthBytes != 2 &&
      value.widthBytes != 4 && value.widthBytes != 8)
    return false;
  if (value.rank == 0 || value.rank > kMaximumNumericRank ||
      value.channelCount == 0)
    return false;
  if (value.widthBytes == 1 && value.endian != Endian::NOT_APPLICABLE)
    return false;
  if (value.widthBytes != 1 && value.endian == Endian::NOT_APPLICABLE)
    return false;
  uint64_t elements = 1;
  for (uint8_t i = 0; i < value.rank; ++i) {
    if (value.dimensions[i] == 0 ||
        !checkedMultiply(elements, value.dimensions[i], elements))
      return false;
  }
  return true;
}

inline std::vector<uint8_t> encodeTextParams(const TextParams& value) {
  if (static_cast<uint8_t>(value.encoding) >
        static_cast<uint8_t>(TextEncoding::UTF32_BE) ||
      static_cast<uint8_t>(value.flavor) >
        static_cast<uint8_t>(TextFlavor::LOG) ||
      static_cast<uint8_t>(value.newline) >
        static_cast<uint8_t>(NewlineKind::CR))
    quit("Invalid text routed-profile parameters.");
  CanonicalWriter writer;
  writer.putByte(static_cast<uint8_t>(value.encoding));
  writer.putByte(static_cast<uint8_t>(value.flavor));
  writer.putByte(static_cast<uint8_t>(value.newline));
  writer.putBool(value.hasBom);
  writer.putBool(value.preserveInvalidSequences);
  writer.putByte(value.delimiter);
  writer.putByte(value.quote);
  writer.putByte(value.escape);
  writer.putUleb128(value.fixedColumnSchemaId);
  return writer.take();
}

inline bool decodeTextParams(const std::vector<uint8_t>& bytes, TextParams& value) {
  CanonicalReader reader(bytes);
  uint8_t encoding = 0, flavor = 0, newline = 0;
  uint64_t schemaId = 0;
  if (!reader.readByte(encoding) || encoding > static_cast<uint8_t>(TextEncoding::UTF32_BE) ||
      !reader.readByte(flavor) || flavor > static_cast<uint8_t>(TextFlavor::LOG) ||
      !reader.readByte(newline) || newline > static_cast<uint8_t>(NewlineKind::CR) ||
      !reader.readBool(value.hasBom) ||
      !reader.readBool(value.preserveInvalidSequences) ||
      !reader.readByte(value.delimiter) || !reader.readByte(value.quote) ||
      !reader.readByte(value.escape) || !reader.readUleb128(schemaId) ||
      schemaId > std::numeric_limits<uint32_t>::max() || !reader.atEnd())
    return false;
  value.encoding = static_cast<TextEncoding>(encoding);
  value.flavor = static_cast<TextFlavor>(flavor);
  value.newline = static_cast<NewlineKind>(newline);
  value.fixedColumnSchemaId = static_cast<uint32_t>(schemaId);
  return true;
}

inline std::vector<uint8_t> encodeRecordParams(const RecordParams& value) {
  if (value.stride == 0 || value.alignment == 0 ||
      value.alignment > value.stride || value.phase >= value.stride ||
      value.fieldPhaseMask.size() > value.stride ||
      value.embeddedSchema.size() > kMaximumEmbeddedSchemaBytes)
    quit("Invalid record routed-profile parameters.");
  CanonicalWriter writer;
  writer.putUleb128(value.stride);
  writer.putUleb128(value.alignment);
  writer.putUleb128(value.phase);
  writer.putUleb128(value.recordCountHint);
  writer.putUleb128(value.schemaId);
  writer.putBlob(value.fieldPhaseMask);
  writer.putBlob(value.embeddedSchema);
  return writer.take();
}

inline bool decodeRecordParams(const std::vector<uint8_t>& bytes, RecordParams& value) {
  CanonicalReader reader(bytes);
  uint64_t stride = 0, alignment = 0, phase = 0, schemaId = 0;
  if (!reader.readUleb128(stride) || stride > std::numeric_limits<uint32_t>::max() ||
      !reader.readUleb128(alignment) || alignment > std::numeric_limits<uint32_t>::max() ||
      !reader.readUleb128(phase) || phase > std::numeric_limits<uint32_t>::max() ||
      !reader.readUleb128(value.recordCountHint) ||
      !reader.readUleb128(schemaId) || schemaId > std::numeric_limits<uint32_t>::max() ||
      !reader.readBlob(value.fieldPhaseMask, stride) ||
      !reader.readBlob(value.embeddedSchema, kMaximumEmbeddedSchemaBytes) ||
      !reader.atEnd() || stride == 0 || alignment == 0 || alignment > stride ||
      phase >= stride)
    return false;
  value.stride = static_cast<uint32_t>(stride);
  value.alignment = static_cast<uint32_t>(alignment);
  value.phase = static_cast<uint32_t>(phase);
  value.schemaId = static_cast<uint32_t>(schemaId);
  return true;
}

inline std::vector<uint8_t> encodeNumericParams(const NumericParams& value) {
  if (!validNumericParams(value))
    quit("Invalid numeric routed-profile parameters.");
  CanonicalWriter writer;
  writer.putByte(static_cast<uint8_t>(value.scalarKind));
  writer.putByte(value.widthBytes);
  writer.putByte(static_cast<uint8_t>(value.endian));
  writer.putByte(value.rank);
  writer.putByte(value.channelCount);
  writer.putByte(static_cast<uint8_t>(value.layout));
  for (uint8_t i = 0; i < value.rank; ++i)
    writer.putUleb128(value.dimensions[i]);
  return writer.take();
}

inline bool decodeNumericParams(const std::vector<uint8_t>& bytes, NumericParams& value) {
  CanonicalReader reader(bytes);
  uint8_t scalar = 0, endian = 0, layout = 0;
  if (!reader.readByte(scalar) || scalar > static_cast<uint8_t>(ScalarKind::IEEE754_FLOAT) ||
      !reader.readByte(value.widthBytes) || !reader.readByte(endian) ||
      endian > static_cast<uint8_t>(Endian::NOT_APPLICABLE) ||
      !reader.readByte(value.rank) || !reader.readByte(value.channelCount) ||
      !reader.readByte(layout) || layout > static_cast<uint8_t>(LayoutKind::INTERLEAVED) ||
      value.rank == 0 || value.rank > kMaximumNumericRank)
    return false;
  value.scalarKind = static_cast<ScalarKind>(scalar);
  value.endian = static_cast<Endian>(endian);
  value.layout = static_cast<LayoutKind>(layout);
  value.dimensions.fill(0);
  for (uint8_t i = 0; i < value.rank; ++i) {
    if (!reader.readUleb128(value.dimensions[i]))
      return false;
  }
  return reader.atEnd() && validNumericParams(value);
}

inline std::vector<uint8_t> encodeRasterParams(const RasterParams& value) {
  const bool bayerConsistent = value.pixelLayout == PixelLayout::BAYER
    ? value.bayerPattern != BayerPattern::NONE
    : value.bayerPattern == BayerPattern::NONE;
  if (value.width == 0 || value.height == 0 || value.rowStride == 0 ||
      value.bitsPerSample == 0 || value.channels == 0 ||
      static_cast<uint8_t>(value.pixelLayout) >
        static_cast<uint8_t>(PixelLayout::UNKNOWN_TYPED) ||
      static_cast<uint8_t>(value.bayerPattern) >
        static_cast<uint8_t>(BayerPattern::GBRG) || !bayerConsistent)
    quit("Invalid raster routed-profile parameters.");
  CanonicalWriter writer;
  writer.putUleb128(value.width);
  writer.putUleb128(value.height);
  writer.putUleb128(value.rowStride);
  writer.putByte(value.bitsPerSample);
  writer.putByte(value.channels);
  writer.putByte(static_cast<uint8_t>(value.pixelLayout));
  writer.putByte(static_cast<uint8_t>(value.bayerPattern));
  return writer.take();
}

inline bool decodeRasterParams(const std::vector<uint8_t>& bytes, RasterParams& value) {
  CanonicalReader reader(bytes);
  uint64_t rowStride = 0;
  uint8_t pixel = 0, bayer = 0;
  if (!reader.readUleb128(value.width) || !reader.readUleb128(value.height) ||
      !reader.readUleb128(rowStride) || rowStride > std::numeric_limits<uint32_t>::max() ||
      !reader.readByte(value.bitsPerSample) || !reader.readByte(value.channels) ||
      !reader.readByte(pixel) || pixel > static_cast<uint8_t>(PixelLayout::UNKNOWN_TYPED) ||
      !reader.readByte(bayer) || bayer > static_cast<uint8_t>(BayerPattern::GBRG) ||
      !reader.atEnd() || value.width == 0 || value.height == 0 ||
      rowStride == 0 || value.bitsPerSample == 0 || value.channels == 0)
    return false;
  value.rowStride = static_cast<uint32_t>(rowStride);
  value.pixelLayout = static_cast<PixelLayout>(pixel);
  value.bayerPattern = static_cast<BayerPattern>(bayer);
  return value.pixelLayout == PixelLayout::BAYER
    ? value.bayerPattern != BayerPattern::NONE
    : value.bayerPattern == BayerPattern::NONE;
}

inline std::vector<uint8_t> encodeAudioParams(const AudioParams& value) {
  if (value.bitsPerSample == 0 || value.channels == 0 || value.sampleRate == 0 ||
      static_cast<uint8_t>(value.sampleKind) >
        static_cast<uint8_t>(SampleKind::IEEE754_PCM) ||
      static_cast<uint8_t>(value.endian) >
        static_cast<uint8_t>(Endian::NOT_APPLICABLE) ||
      static_cast<uint8_t>(value.interleave) >
        static_cast<uint8_t>(Interleave::PLANAR))
    quit("Invalid audio routed-profile parameters.");
  CanonicalWriter writer;
  writer.putByte(static_cast<uint8_t>(value.sampleKind));
  writer.putByte(value.bitsPerSample);
  writer.putUleb128(value.channels);
  writer.putUleb128(value.sampleRate);
  writer.putByte(static_cast<uint8_t>(value.endian));
  writer.putByte(static_cast<uint8_t>(value.interleave));
  return writer.take();
}

inline bool decodeAudioParams(const std::vector<uint8_t>& bytes, AudioParams& value) {
  CanonicalReader reader(bytes);
  uint8_t sample = 0, endian = 0, interleave = 0;
  uint64_t channels = 0, sampleRate = 0;
  if (!reader.readByte(sample) || sample > static_cast<uint8_t>(SampleKind::IEEE754_PCM) ||
      !reader.readByte(value.bitsPerSample) || !reader.readUleb128(channels) ||
      channels > std::numeric_limits<uint16_t>::max() ||
      !reader.readUleb128(sampleRate) || sampleRate > std::numeric_limits<uint32_t>::max() ||
      !reader.readByte(endian) || endian > static_cast<uint8_t>(Endian::NOT_APPLICABLE) ||
      !reader.readByte(interleave) || interleave > static_cast<uint8_t>(Interleave::PLANAR) ||
      !reader.atEnd() || value.bitsPerSample == 0 || channels == 0 || sampleRate == 0)
    return false;
  value.sampleKind = static_cast<SampleKind>(sample);
  value.channels = static_cast<uint16_t>(channels);
  value.sampleRate = static_cast<uint32_t>(sampleRate);
  value.endian = static_cast<Endian>(endian);
  value.interleave = static_cast<Interleave>(interleave);
  return true;
}

inline std::vector<uint8_t> encodeCodeParams(const CodeParams& value) {
  if (static_cast<uint8_t>(value.isa) >
        static_cast<uint8_t>(IsaKind::DEC_ALPHA) ||
      (value.addressBits != 32 && value.addressBits != 64))
    quit("Invalid machine-code routed-profile parameters.");
  CanonicalWriter writer;
  writer.putByte(static_cast<uint8_t>(value.isa));
  writer.putByte(value.addressBits);
  writer.putUleb128(value.virtualBase);
  writer.putUleb128(value.sourceOffset);
  writer.putUleb128(value.sectionFlags);
  return writer.take();
}

inline bool decodeCodeParams(const std::vector<uint8_t>& bytes, CodeParams& value) {
  CanonicalReader reader(bytes);
  uint8_t isa = 0;
  uint64_t sectionFlags = 0;
  if (!reader.readByte(isa) || isa > static_cast<uint8_t>(IsaKind::DEC_ALPHA) ||
      !reader.readByte(value.addressBits) ||
      !reader.readUleb128(value.virtualBase) ||
      !reader.readUleb128(value.sourceOffset) ||
      !reader.readUleb128(sectionFlags) ||
      sectionFlags > std::numeric_limits<uint32_t>::max() || !reader.atEnd() ||
      (value.addressBits != 32 && value.addressBits != 64))
    return false;
  value.isa = static_cast<IsaKind>(isa);
  value.sectionFlags = static_cast<uint32_t>(sectionFlags);
  return true;
}

} // namespace routed
