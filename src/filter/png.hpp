#pragma once

#include "Filter.hpp"
#include "../file/File.hpp"
#include <cstdint>
#include <limits>

/**
 * 8/24/32-bit png image data encode/decode
 * filter bytes from individual lines go to a separate header
 */
class PngFilter : public Filter {
private:
  int stride = 3; //1: Gray/Indexed, 3: RGB, 4: RGBA
  int width = 0;

  struct Geometry {
    uint64_t rowCount = 0;
    uint64_t pixelBytes = 0;
  };

  static Geometry validateGeometry(uint64_t size, int rowWidth,
                                   int pixelStride) {
    if (rowWidth <= 0 ||
        (pixelStride != 1 && pixelStride != 3 && pixelStride != 4) ||
        rowWidth < pixelStride)
      quit("Corrupted PNG transform: invalid row geometry.");

    const uint64_t lineWidth = static_cast<uint64_t>(rowWidth) + 1;
    if (size == 0 || size % lineWidth != 0)
      quit("Corrupted PNG transform: incomplete scanline coverage.");
    const uint64_t rowCount = size / lineWidth;
    if (rowCount == 0 ||
        rowCount > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
        rowCount > std::numeric_limits<uint64_t>::max() /
          static_cast<uint64_t>(rowWidth))
      quit("Corrupted PNG transform: row count is outside its limit.");
    const uint64_t pixelBytes = rowCount * static_cast<uint64_t>(rowWidth);
    if (pixelBytes != size - rowCount ||
        pixelBytes == 0 || pixelBytes > (UINT64_C(1) << 31))
      quit("Corrupted PNG transform: pixel buffer length is outside its limit.");
    return {rowCount, pixelBytes};
  }

  static uint8_t readRequiredByte(File* input, const char* message) {
    if (input == nullptr)
      quit(message);
    const int value = input->getchar();
    if (value == EOF)
      quit(message);
    return static_cast<uint8_t>(value);
  }
public:

  void setWidth(int w) {
    this->width = w;
  }
  void setStride(int stride) {
    this->stride = stride;
  }

  void encode(File *in, File *out, uint64_t size, int width, int & headerSize) override {
    if (in == nullptr || out == nullptr)
      quit("PNG transform input or output is unavailable.");
    const Geometry geometry = validateGeometry(size, width, stride);
    headerSize = static_cast<int>(geometry.rowCount); // = number of rows
    RingBuffer<uint8_t> filterBuffer(
      nextPowerOf2(static_cast<uint32_t>(geometry.rowCount)));
    RingBuffer<uint8_t> pixelBuffer(
      nextPowerOf2(static_cast<uint32_t>(geometry.pixelBytes)));
    if (filterBuffer.size() < geometry.rowCount ||
        pixelBuffer.size() < geometry.pixelBytes)
      quit("PNG transform buffer sizing overflow.");
    for( uint64_t line = 0; line < geometry.rowCount; line++ ) {
      uint8_t filter = readRequiredByte(
        in, "PNG transform input is truncated.");
      filterBuffer.add(filter);
      for (int x = 0; x < width; x++) {
        uint8_t c1 = readRequiredByte(
          in, "PNG transform input is truncated.");
        switch (filter) {
          case 0: {
            break;
          }
          case 1: {
            c1=(static_cast<uint8_t>(c1 + (x < stride ? 0 : pixelBuffer(stride))));
            break;
          }
          case 2: {
            c1=(static_cast<uint8_t>(c1 + (line == 0 ? 0 : pixelBuffer(width))));
            break;
          }
          case 3: {
            c1 = (static_cast<uint8_t>(c1 + (((line == 0 ? 0 : pixelBuffer(width)) + (x < stride ? 0 : pixelBuffer(stride))) >> 1)));
            break;
          }
          case 4: {
            c1 = (static_cast<uint8_t>(c1 + paeth(
              x < stride ? 0 : pixelBuffer(stride),
              line == 0 ? 0 : pixelBuffer(width),
              line == 0 || x < stride ? 0 : pixelBuffer(
                static_cast<uint32_t>(width) +
                static_cast<uint32_t>(stride)))));
            break;
          }
          default:
            quit("PNG transform input has an invalid filter type.");
        }
        pixelBuffer.add(c1);
      }
    }
    uint32_t len1 = filterBuffer.getpos();
    uint32_t len2 = pixelBuffer.getpos();
    for (uint32_t i = 0; i < len1; i++)
      out->putChar(filterBuffer[i]);
    for (uint32_t i = 0; i < len2; i++)
      out->putChar(pixelBuffer[i]);
  }

  uint64_t decode(File *in, File *out, FMode fMode, uint64_t size, uint64_t &diffFound) override {
    if (in == nullptr || out == nullptr)
      quit("PNG decoder input or output is unavailable.");
    const Geometry geometry = validateGeometry(size, width, stride);
    RingBuffer<uint8_t> filterBuffer(
      nextPowerOf2(static_cast<uint32_t>(geometry.rowCount)));
    RingBuffer<uint8_t> pixelBuffer(
      nextPowerOf2(static_cast<uint32_t>(geometry.pixelBytes)));
    if (filterBuffer.size() < geometry.rowCount ||
        pixelBuffer.size() < geometry.pixelBytes)
      quit("Corrupted PNG transform: buffer sizing overflow.");
    for (uint64_t line = 0; line < geometry.rowCount; line++) {
      uint8_t filter = readRequiredByte(
        in, "Corrupted PNG transform: truncated filter stream.");
      if (filter > 4)
        quit("Corrupted PNG transform: invalid filter type.");
      filterBuffer.add(filter);
    }
    uint64_t p = 0;
    for (uint64_t line = 0; line < geometry.rowCount; line++) {
      uint8_t filter = filterBuffer[static_cast<uint32_t>(line)];
      if (fMode == FMode::FDECOMPRESS) {
        out->putChar(filter);
      }
      else if (fMode == FMode::FCOMPARE) {
        p++;
        if (filter != out->getchar() && (diffFound == 0)) {
          diffFound = p;
        }
      }
      for (int x = 0; x < width; x++) {
        uint8_t c1 = readRequiredByte(
          in, "Corrupted PNG transform: truncated pixel stream.");
        uint8_t c = c1;
        switch (filter) {
          case 0: {
            break;
          }
          case 1: {
            c1 = (static_cast<uint8_t>(c1 - (x < stride ? 0 : pixelBuffer(stride))));
            break;
          }
          case 2: {
            c1 = (static_cast<uint8_t>(c1 - (line == 0 ? 0 : pixelBuffer(width))));
            break;
          }
          case 3: {
            c1 = (static_cast<uint8_t>(c1 - (((line == 0 ? 0 : pixelBuffer(width)) + (x < stride ? 0 : pixelBuffer(stride))) >> 1)));
            break;
          }
          case 4: {
            c1 = (static_cast<uint8_t>(c1 - paeth(
              x < stride ? 0 : pixelBuffer(stride),
              line == 0 ? 0 : pixelBuffer(width),
              line == 0 || x < stride ? 0 : pixelBuffer(
                static_cast<uint32_t>(width) +
                static_cast<uint32_t>(stride)))));
            break;
          }
          default:
            quit("Corrupted PNG transform: invalid filter type.");
        }
        pixelBuffer.add(c);
        if (fMode == FMode::FDECOMPRESS) {
          out->putChar(c1);
        }
        else if (fMode == FMode::FCOMPARE) {
          p++;
          if (c1 != out->getchar() && (diffFound == 0)) {
            diffFound = p;
          }
        }
      }
    }
    return size;
  }
};
