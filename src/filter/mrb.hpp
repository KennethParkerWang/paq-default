#pragma once

#include "Filter.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>

class MrbRleFilter : Filter {
private:
  static constexpr uint64_t kMaximumDiffCount = 4095;
  // MRB blockInfo publishes 12-bit width and height fields. Even the widest
  // 8-bit row is at most 4096 bytes, so a valid decoded raster fits in 2^24.
  static constexpr uint64_t kMaximumRasterBytes = UINT64_C(1) << 24;

  int width;
  int height;

  int encodeRLE(uint8_t* dst, uint8_t* ptr, int src_end, int maxlen) {
    int i = 0;
    int ind = 0;
    for (ind = 0; ind < src_end; ) {
      if (i > maxlen) return i;
      if (ptr[ind + 0] != ptr[ind + 1] || ptr[ind + 1] != ptr[ind + 2]) {
        // Guess how many non repeating bytes we have
        int j = 0;
        for (j = ind + 1; j < (src_end); j++)
          if ((ptr[j + 0] == ptr[j + 1] && ptr[j + 2] == ptr[j + 0]) || ((j - ind) >= 127)) break;
        int pixels = j - ind;
        if (j + 1 == src_end && pixels < 8)pixels++;
        dst[i++] = 0x80 | pixels;
        for (int cnt = 0; cnt < pixels; cnt++) {
          dst[i++] = ptr[ind + cnt];
          if (i > maxlen) return i;
        }
        ind = ind + pixels;
      }
      else {
        // Get the number of repeating bytes
        int j = 0;
        for (j = ind + 1; j < (src_end); j++)
          if (ptr[j + 0] != ptr[j + 1]) break;
        int pixels = j - ind + 1;
        if (j == src_end && pixels < 4) {
          pixels--;
          dst[i] = uint8_t(0x80 | pixels);
          i++;
          if (i > maxlen) return i;
          for (int cnt = 0; cnt < pixels; cnt++) {
            dst[i] = ptr[ind + cnt];
            i++;
            if (i > maxlen) return i;
          }
          ind = ind + pixels;
        }
        else {
          j = pixels;
          while (pixels > 127) {
            dst[i++] = 127;
            dst[i++] = ptr[ind];
            if (i > maxlen) return i;
            pixels = pixels - 127;
          }
          if (pixels > 0) {
            if (j == src_end) pixels--;
            dst[i++] = pixels;
            dst[i++] = ptr[ind];
            if (i > maxlen) return i;
          }
          ind = ind + j;
        }
      }
    }
    return i;
  }

public:
  void setInfo(int width, int height) {
    this->width = width;
    this->height = height;
  }
  void encode(File* in, File* out, uint64_t size, int info, int& /*headerSize*/) override {
    uint64_t savepos = in->curPos();
    int totalSize = (width)*height;
    Array<uint8_t, 1> ptrin(totalSize + 4);
    Array<uint8_t, 1> ptr(size + 4);
    Array<uint32_t> diffpos(4096);
    uint32_t count = 0;
    uint8_t value = 0;
    int diffcount = 0;
    // decode RLE
    for (int i = 0; i < totalSize; ++i) {
      if ((count & 0x7F) == 0) {
        count = in->getchar();
        value = in->getchar();
      }
      else if (count & 0x80) {
        value = in->getchar();
      }
      count--;
      ptrin[i] = value;
    }
    // encode RLE
    int a = encodeRLE(&ptr[0], &ptrin[0], totalSize, size);
    assert(a < (size + 4));
    // compare to original and output diff data
    in->setpos(savepos);
    for (int i = 0; i < size; i++) {
      uint8_t b = ptr[i], c = in->getchar();
      if (diffcount == 4095 || diffcount > (size / 2) || i > 0xFFFFFF) return; // fail
      if (b != c) {
        if (diffcount < 4095)
          diffpos[diffcount++] = static_cast<uint32_t>(c) |
                                 (static_cast<uint32_t>(i) << 8);
      }
    }
    out->putChar((diffcount >> 8) & 255); out->putChar(diffcount & 255);
    if (diffcount > 0)
      out->blockWrite((uint8_t*)&diffpos[0], diffcount * 4);
    out->put32(size);
    out->blockWrite(&ptrin[0], totalSize);
  }

  uint64_t decode(File* in, File* out, FMode fMode, uint64_t  size, uint64_t& diffFound) override {
    constexpr uint64_t fixedHeaderBytes = 2 + 4;
    if (size < fixedHeaderBytes)
      quit("Corrupted MRB transform: truncated header.");

    uint8_t diffCountBytes[2] = {0, 0};
    if (in->blockRead(diffCountBytes, sizeof(diffCountBytes)) !=
        sizeof(diffCountBytes))
      quit("Corrupted MRB transform: truncated difference count.");
    const uint64_t diffcount =
      (static_cast<uint64_t>(diffCountBytes[0]) << 8) |
      diffCountBytes[1];
    if (diffcount > kMaximumDiffCount)
      quit("Corrupted MRB transform: difference table is too large.");

    const uint64_t diffBytes = diffcount * sizeof(uint32_t);
    if (diffBytes > size - fixedHeaderBytes)
      quit("Corrupted MRB transform: difference table is truncated.");

    Array<uint32_t> diffpos(4096);
    if (diffBytes != 0 &&
        in->blockRead(reinterpret_cast<uint8_t*>(&diffpos[0]), diffBytes) !=
          diffBytes)
      quit("Corrupted MRB transform: truncated difference table.");

    uint8_t lengthBytes[4] = {0, 0, 0, 0};
    if (in->blockRead(lengthBytes, sizeof(lengthBytes)) != sizeof(lengthBytes))
      quit("Corrupted MRB transform: missing restored length.");
    const uint64_t restoredLength =
      (static_cast<uint64_t>(lengthBytes[0]) << 24) |
      (static_cast<uint64_t>(lengthBytes[1]) << 16) |
      (static_cast<uint64_t>(lengthBytes[2]) << 8) |
      lengthBytes[3];
    const uint64_t rasterBytes = size - fixedHeaderBytes - diffBytes;
    if (rasterBytes == 0 || rasterBytes > kMaximumRasterBytes ||
        rasterBytes > static_cast<uint64_t>(INT_MAX))
      quit("Corrupted MRB transform: invalid raster length.");
    // PackBits-style literal packets can be larger than the raw raster, but
    // this encoder emits at most two bytes per raster byte (plus lookahead).
    const uint64_t maximumRestoredLength = rasterBytes * 2 + 4;
    if (restoredLength == 0 || restoredLength > maximumRestoredLength ||
        restoredLength > static_cast<uint64_t>(INT_MAX))
      quit("Corrupted MRB transform: invalid restored length.");

    Array<uint8_t, 1> fptr(rasterBytes + 4);
    Array<uint8_t, 1> ptr(restoredLength + 4);
    if (in->blockRead(&fptr[0], rasterBytes) != rasterBytes)
      quit("Corrupted MRB transform: truncated raster data.");
    const int encodedLength = encodeRLE(
      &ptr[0], &fptr[0], static_cast<int>(rasterBytes),
      static_cast<int>(restoredLength));
    // encodeRLE() checks its limit after each one- or two-byte packet.  It can
    // therefore return at most maxlen + 2, and the four guard bytes above are
    // sufficient.  Legacy encoders intentionally allowed this overshoot and
    // stored differences only for the restoredLength-byte prefix.
    if (encodedLength < 0 ||
        static_cast<uint64_t>(encodedLength) > restoredLength + 2)
      quit("Corrupted MRB transform: reconstructed RLE stream is too large.");

    uint64_t previousOffset = 0;
    for (uint64_t index = 0; index < diffcount; ++index) {
      const uint64_t offset = diffpos[index] >> 8;
      if (offset >= restoredLength || (index != 0 && offset <= previousOffset))
        quit("Corrupted MRB transform: invalid difference offset.");
      ptr[offset] = static_cast<uint8_t>(diffpos[index]);
      previousOffset = offset;
    }

    //Write out or compare
    if (fMode == FMode::FDECOMPRESS) {
      out->blockWrite(&ptr[0], restoredLength);
    }
    else if (fMode == FMode::FCOMPARE) {
      for (uint64_t i = 0; i < restoredLength; i++) {
        uint8_t b = ptr[i];
        if (b != out->getchar() && !diffFound) diffFound = out->curPos();
      }
    }
    return restoredLength;
  }
};
