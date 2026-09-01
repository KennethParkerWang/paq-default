#pragma once

#include "Filter.hpp"
#include "../Array.hpp"
#include "../file/File.hpp"

namespace base64 {
  constexpr bool isdigit(int8_t c) {
    return c >= '0' && c <= '9';
  }

  constexpr bool islower(int8_t c) {
    return c >= 'a' && c <= 'z';
  }

  constexpr bool isupper(int8_t c) {
    return c >= 'A' && c <= 'Z';
  }

  constexpr bool isalpha(int8_t c) {
    return islower(c) || isupper(c);
  }

  constexpr bool isalnum(int8_t c) {
    return isalpha(c) || isdigit(c);
  }

  static constexpr char table1[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
} // namespace base64

class Base64Filter : Filter {
private:
  static char valueB(char c) {
    const char* p = strchr(base64::table1, c);
    if (p != nullptr) {
      return static_cast<char>(p - base64::table1);
    }
    return 0;
  }

  static bool isBase64(uint8_t c) {
    return (base64::isalnum(c) || (c == '+') || (c == '/') || (c == 10) || (c == 13));
  }

public:
  void encode(File* in, File* out, uint64_t size, int  /*info*/, int& /*headerSize*/) override {
    uint64_t inLen = 0;
    int i = 0;
    int lineSize = 0;
    uint8_t b = 0;
    uint8_t tlf = 0;
    uint8_t src[4];
    uint64_t b64Mem = (size >> 2) * 3 + 10;
    Array<uint8_t> ptr(b64Mem);
    int olen = 5;

    while (b = in->getchar(), inLen++, (b != '=') && isBase64(b) && inLen <= size) {
      if (b == 13 || b == 10) {
        if (lineSize == 0) {
          lineSize = inLen;
          tlf = b;
        }
        if (tlf != b) {
          tlf = 0;
        }
        continue;
      }
      src[i++] = b;
      if (i == 4) {
        for (int j = 0; j < 4; j++) {
          src[j] = valueB(src[j]);
        }
        src[0] = (src[0] << 2) + ((src[1] & 0x30) >> 4);
        src[1] = ((src[1] & 0xf) << 4) + ((src[2] & 0x3c) >> 2);
        src[2] = ((src[2] & 0x3) << 6) + src[3];

        ptr[olen++] = src[0];
        ptr[olen++] = src[1];
        ptr[olen++] = src[2];
        i = 0;
      }
    }

    if (i != 0) {
      for (int j = i; j < 4; j++) {
        src[j] = 0;
      }

      for (int j = 0; j < 4; j++) {
        src[j] = valueB(src[j]);
      }

      src[0] = (src[0] << 2) + ((src[1] & 0x30) >> 4);
      src[1] = ((src[1] & 0xf) << 4) + ((src[2] & 0x3c) >> 2);
      src[2] = ((src[2] & 0x3) << 6) + src[3];

      for (int j = 0; (j < i - 1); j++) {
        ptr[olen++] = src[j];
      }
    }
    ptr[0] = lineSize & 255;
    ptr[1] = size & 255;
    ptr[2] = (size >> 8) & 255;
    ptr[3] = (size >> 16) & 255;
    const uint8_t newlineFlag = tlf == 10 ? 128u : tlf != 0 ? 64u : 0u;
    ptr[4] = static_cast<uint8_t>(((size >> 24) & 63u) | newlineFlag);
    out->blockWrite(&ptr[0], olen);
  }

  uint64_t decode(File* in, File* out, FMode fMode, uint64_t size,
                  uint64_t& diffFound) override {
    if (in == nullptr || out == nullptr || size < 5)
      quit("Corrupted Base64 transform header.");
    const uint64_t start = in->curPos();
    if (start > UINT64_MAX - size)
      quit("Corrupted Base64 transform length.");
    const uint64_t end = start + size;
    auto readByte = [&]() -> int {
      if (in->curPos() >= end)
        return EOF;
      return in->getchar();
    };
    uint8_t inn[3] = {0, 0, 0};
    int len = 0;
    int blocksOut = 0;
    const int lineSize = readByte();
    const int length0 = readByte();
    const int length1 = readByte();
    const int length2 = readByte();
    const int flags = readByte();
    if (lineSize == EOF || length0 == EOF || length1 == EOF ||
        length2 == EOF || flags == EOF)
      quit("Corrupted Base64 transform header.");
    const uint64_t outLen = static_cast<uint64_t>(length0) |
      (static_cast<uint64_t>(length1) << 8) |
      (static_cast<uint64_t>(length2) << 16) |
      (static_cast<uint64_t>(flags & 63) << 24);
    uint8_t tlf = static_cast<uint8_t>(flags & 192);
    tlf = (tlf & 192);
    if (tlf == 128) {
      tlf = 10; // LF: 10
    }
    else if (tlf == 64) {
      tlf = 13; // CR: 13
    }
    else {
      tlf = 0;
    }

    uint64_t produced = 0;
    auto emit = [&](uint8_t byte) {
      if (produced >= outLen)
        quit("Corrupted Base64 transform output length.");
      if (fMode == FMode::FDECOMPRESS)
        out->putChar(byte);
      else if (fMode == FMode::FCOMPARE &&
               byte != out->getchar() && diffFound == 0)
        diffFound = produced + 1;
      ++produced;
    };

    while (produced < outLen) {
      // The final group may contain only one or two decoded bytes. Base64
      // defines the missing low-order bytes as zero when forming its padded
      // quartet; do not reuse bytes from the previous group.
      inn[0] = inn[1] = inn[2] = 0;
      len = 0;
      for (int i = 0; i < 3; i++) {
        const int c = readByte();
        if (c != EOF) {
          inn[i] = static_cast<uint8_t>(c);
          len++;
        }
        else {
          inn[i] = 0;
        }
      }
      if (len != 0) {
        const uint8_t in0 = inn[0];
        const uint8_t in1 = inn[1];
        const uint8_t in2 = inn[2];
        emit(base64::table1[in0 >> 2]);
        emit(base64::table1[((in0 & 0x03) << 4) |
                            ((in1 & 0xf0) >> 4)]);
        emit(len > 1 ? base64::table1[((in1 & 0x0f) << 2) |
                                      ((in2 & 0xc0) >> 6)] : '=');
        emit(len > 2 ? base64::table1[in2 & 0x3f] : '=');
        blocksOut++;
      }
      else {
        quit("Corrupted Base64 transform payload.");
      }
      if (blocksOut >= (lineSize / 4) && lineSize != 0) { //no lf if lineSize==0
        if (blocksOut != 0 && in->curPos() < end && produced < outLen) {
          if (tlf != 0) {
            emit(tlf);
          }
          else {
            emit(13);
            emit(10);
          }
        }
        blocksOut = 0;
      }
    }
    if (in->curPos() != end)
      quit("Corrupted Base64 transform contains trailing payload bytes.");
    return outLen;
  }
};
