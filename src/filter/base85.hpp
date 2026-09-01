#pragma once

#include "Filter.hpp"
#include "../Array.hpp"
#include "../file/File.hpp"
#include "../CharacterNames.hpp"

constexpr int powers[5] = { 85 * 85 * 85 * 85, 85 * 85 * 85, 85 * 85, 85, 1 };

class Base85Filter : Filter {
public:
  void encode(File* in, File* out, uint64_t size, int  /*info*/, int& /*headerSize*/) override {
    int lfp = 0;
    int tlf = 0;
    int b85mem = (size >> 2) * 5 + 100;
    Array<uint8_t, 1> ptr(b85mem);
    int olen = 5;
    int c;
    int count = 0;
    uint32_t tuple = 0;
    for (int f = 0; f < size; f++) {
      c = in->getchar();
      if (olen + 10 > b85mem) {
        count = 0;
        break;
      }
      if (c == CARRIAGE_RETURN || c == NEW_LINE) {
        if (lfp == 0) {
          lfp = f;
          tlf = c;
        }
        if (tlf != c)
          tlf = 0;
        continue;
      }
      if (c == 'z' && count == 0) {
        if (olen + 10 > b85mem) {
          count = 0;
          break;
        }
        for (int i = 1; i < 5; i++)
          ptr[olen++] = 0;
        continue;
      }
      if (c == EOF) {
        if (olen + 10 > b85mem) {
          count = 0;
          break;
        }
        if (count > 0) {
          tuple += powers[count - 1];
          for (int i = 1; i < count; i++)
            ptr[olen++] = tuple >> ((4 - i) * 8);
        }
        break;
      }
      tuple += (c - '!') * powers[count++];
      if (count == 5) {
        if (olen > b85mem + 10) {
          count = 0;
          break;
        }
        for (int i = 1; i < count; i++)
          ptr[olen++] = tuple >> ((4 - i) * 8);
        tuple = 0;
        count = 0;
      }
    }
    if (count > 0) {
      tuple += powers[count - 1];
      for (int i = 1; i < count; i++)
        ptr[olen++] = tuple >> ((4 - i) * 8);
    }
    ptr[0] = lfp & 255; //nl lenght
    ptr[1] = size & 255;
    ptr[2] = size >> 8 & 255;
    ptr[3] = size >> 16 & 255;
    const uint8_t newlineFlag = tlf == 10 ? 128u : tlf != 0 ? 64u : 0u;
    ptr[4] = static_cast<uint8_t>(((size >> 24) & 63u) | newlineFlag);
    out->blockWrite(&ptr[0], olen);
  }

  uint64_t decode(File* in, File* out, FMode fMode, uint64_t size,
                  uint64_t& diffFound) override {
    if (in == nullptr || out == nullptr || size < 5)
      quit("Corrupted Base85 transform header.");
    const uint64_t start = in->curPos();
    if (start > UINT64_MAX - size)
      quit("Corrupted Base85 transform length.");
    const uint64_t end = start + size;
    auto readByte = [&]() -> int {
      if (in->curPos() >= end)
        return EOF;
      return in->getchar();
    };
    const int nlsize = readByte();
    const int length0 = readByte();
    const int length1 = readByte();
    const int length2 = readByte();
    int tlf = readByte();
    if (nlsize == EOF || length0 == EOF || length1 == EOF ||
        length2 == EOF || tlf == EOF)
      quit("Corrupted Base85 transform header.");
    const uint64_t outlen = static_cast<uint64_t>(length0) |
      (static_cast<uint64_t>(length1) << 8) |
      (static_cast<uint64_t>(length2) << 16) |
      (static_cast<uint64_t>(tlf & 63) << 24);
    tlf = (tlf & 192);
    if (tlf == 128)
      tlf = NEW_LINE;
    else if (tlf == 64)
      tlf = CARRIAGE_RETURN;
    else
      tlf = 0;
    int c;
    int count = 0;
    int lenlf = 0;
    uint32_t tuple = 0;

    uint64_t produced = 0;
    auto emit = [&](uint8_t byte) {
      if (produced >= outlen)
        quit("Corrupted Base85 transform output length.");
      if (fMode == FMode::FDECOMPRESS)
        out->putChar(byte);
      else if (fMode == FMode::FCOMPARE &&
               byte != out->getchar() && diffFound == 0)
        diffFound = produced + 1;
      ++produced;
    };

    while (produced < outlen) {
      c = readByte();
      if (c != EOF) {
        tuple |= ((uint32_t)c) << ((3 - count++) * 8);
        if (count < 4) continue;
      }
      else if (count == 0) break;
      int i;
      int lim;
      char out[5];
      if (tuple == 0 && count == 4) { // for 0x00000000
        if (nlsize && lenlf >= nlsize) {
          if (tlf)
            emit(static_cast<uint8_t>(tlf));
          else {
            emit(CARRIAGE_RETURN);
            emit(NEW_LINE);
          }
          lenlf = 0;
        }
        emit('z');
      }
      else {
        for (i = 0; i < 5; i++) {
          out[i] = tuple % 85 + '!';
          tuple /= 85;
        }
        lim = 4 - count;
        for (i = 4; i >= lim; i--) {
          if (nlsize && lenlf >= nlsize &&
              ((outlen - produced) >= 5)) {// skip nl if only 5 bytes left
            if (tlf)
              emit(static_cast<uint8_t>(tlf));
            else {
              emit(CARRIAGE_RETURN);
              emit(NEW_LINE);
            }
            lenlf = 0;
          }
          emit(static_cast<uint8_t>(out[i]));
          lenlf++;
        }
      }
      if (c == EOF) break;
      tuple = 0;
      count = 0;
    }
    if (produced != outlen || in->curPos() != end)
      quit("Corrupted Base85 transform payload length.");
    return outlen;
  }
};
