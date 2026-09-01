#pragma once

#include "../Array.hpp"
#include "Filter.hpp"
#include <cstdint>
#include <limits>


#define LZW_TABLE_SIZE 9221

#define LZW_FIND(k) \
  { \
    offset     = ( int ) finalize64((k) * PHI64, 13); \
    int stride = (offset > 0) ? LZW_TABLE_SIZE - offset : 1; \
    while (true) { \
      if ((index = table[offset]) < 0) { \
        index = -offset - 1; \
        break; \
      } else if (dict[index] == int(k)) { \
        break; \
      } \
      offset -= stride; \
      if (offset < 0) \
        offset += LZW_TABLE_SIZE; \
    } \
  }

#define LZW_RESET \
  { \
    for (int i = 0; i < LZW_TABLE_SIZE; table[i] = -1, i++) \
      ; \
  }

static int encodeGif(File *in, File *out, uint64_t len, int &headerSize) {
  constexpr int maximumDifferenceCount = 4096;
  constexpr int maximumHeaderSize = 6 + maximumDifferenceCount * 4;
  if (in == nullptr || out == nullptr || len < 2)
    return 0;
  int codeSize = in->getchar();
  if (codeSize < 2 || codeSize > 8)
    return 0;
  int diffPos = 0;
  int clearPos = 0;
  int bsize = 0;
  int code = 0;
  int offset = 0;
  uint64_t beginIn = in->curPos();
  const uint64_t payloadLength = len - 1;
  if (beginIn > UINT64_MAX - payloadLength)
    return 0;
  const uint64_t endIn = beginIn + payloadLength;
  uint64_t beginOut = out->curPos();
  Array<uint8_t> output(4096);
  headerSize = 6;
  out->putChar(headerSize >> 8);
  out->putChar(headerSize & 255);
  out->putChar(bsize);
  out->putChar(clearPos >> 8);
  out->putChar(clearPos & 255);
  out->putChar(codeSize);
  Array<int> table(LZW_TABLE_SIZE);
  for( int phase = 0; phase < 2; phase++ ) {
    in->setpos(beginIn);
    int bits = codeSize + 1;
    int shift = 0;
    int buffer = 0;
    int blockSize = 0;
    int maxcode = (1u << codeSize) + 1;
    int last = -1;
    Array<int> dict(4096);
    LZW_RESET
    bool end = false;
    bool sawTerminator = false;
    while (in->curPos() < endIn) {
      blockSize = in->getchar();
      if (blockSize == EOF)
        return 0;
      if (blockSize == 0) {
        sawTerminator = true;
        break;
      }
      if (end || static_cast<uint64_t>(blockSize) > endIn - in->curPos())
        return 0;
      for( int i = 0; i < blockSize; i++ ) {
        // Bytes following EOI cannot be reconstructed by this transform.  Do
        // not keep accumulating them: shift would no longer be reduced and
        // could also reach an invalid signed-shift width.
        if (end)
          return 0;
        const int inputByte = in->getchar();
        if (inputByte == EOF)
          return 0;
        buffer |= inputByte << shift;
        shift += 8;
        while( shift >= bits && !end ) {
          code = buffer & ((1u << bits) - 1);
          buffer >>= bits;
          shift -= bits;
          if((bsize == 0) && code != (1u << codeSize)) {
            headerSize += 4;
            out->put32(0);
          }
          if( bsize == 0 ) {
            bsize = blockSize;
          }
          if( code == (1 << codeSize)) {
            if( maxcode > (1 << codeSize) + 1 ) {
              // The inverse's frozen reset schedule cannot represent a clear
              // immediately after the first dictionary insertion.
              if (maxcode <= (1 << codeSize) + 2)
                return 0;
              if((clearPos != 0) && clearPos != 69631 - maxcode ) {
                return 0;
              }
              clearPos = 69631 - maxcode;
            }
            bits = codeSize + 1, maxcode = (1u << codeSize) + 1, last = -1;
            LZW_RESET
          } else if( code == (1u << codeSize) + 1 ) {
            end = true;
          } else if( code > maxcode + 1 ) {
            return 0;
          } else {
            int j = (code <= maxcode ? code : last);
            int size = 1;
            while( j >= (1 << codeSize)) {
              if (j < 0 || j >= 4096 || size >= 4096)
                return 0;
              output[4096 - (size++)] = dict[j] & 255;
              j = dict[j] >> 8;
            }
            if (j < 0 || j >= (1 << codeSize) || size > 4096)
              return 0;
            output[4096 - size] = j;
            if( phase == 1 ) {
              out->blockWrite(&output[4096 - size], size);
            } else {
              if (diffPos > std::numeric_limits<int>::max() - size)
                return 0;
              diffPos += size;
            }
            if( code == maxcode + 1 ) {
              if( phase == 1 ) {
                out->putChar(j);
              } else {
                if (diffPos == std::numeric_limits<int>::max())
                  return 0;
                diffPos++;
              }
            }
            if( last != -1 ) {
              if( ++maxcode >= 8191 ) {
                return 0;
              }
              if( maxcode <= 4095 ) {
                int key = (static_cast<uint32_t>(last) << 8) + j;
                int index = -1;
                LZW_FIND(key)
                dict[maxcode] = key;
                table[(index < 0) ? -index - 1 : offset] = maxcode;
                if( phase == 0 && index > 0 ) {
                  if (headerSize > maximumHeaderSize - 4)
                    return 0;
                  headerSize += 4;
                  j = diffPos - size - static_cast<int>(code == maxcode);
                  if (j < 0)
                    return 0;
                  out->put32(j);
                  diffPos = size + static_cast<int>(code == maxcode);
                }
              }
              if( maxcode >= ((1 << bits) - 1) && bits < 12 ) {
                bits++;
              }
            }
            last = code;
          }
        }
      }
    }
    if (!end || !sawTerminator || in->curPos() != endIn)
      return 0;
  }
  if (bsize <= 0 || bsize > 255 || headerSize < 6 ||
      headerSize > maximumHeaderSize ||
      out->curPos() > static_cast<uint64_t>(std::numeric_limits<int>::max()) ||
      out->curPos() < beginOut ||
      out->curPos() - beginOut <= static_cast<uint64_t>(headerSize))
    return 0;
  diffPos = static_cast<int>(out->curPos());
  out->setpos(beginOut);
  out->putChar(headerSize >> 8);
  out->putChar(headerSize & 255);
  out->putChar(255 - bsize);
  out->putChar((clearPos >> 8) & 255);
  out->putChar(clearPos & 255);
  out->setpos(diffPos);
  return static_cast<int>(in->curPos() == endIn);
}

static uint64_t decodeGif(File *in, uint64_t size, File *out, FMode mode,
                          uint64_t &diffFound) {
  if (in == nullptr || out == nullptr || size < 6)
    quit("Corrupted GIF transform: truncated header.");

  uint64_t remaining = size;
  const auto readRequiredByte = [&]() -> uint8_t {
    if (remaining == 0)
      quit("Corrupted GIF transform: declared length is truncated.");
    const int value = in->getchar();
    if (value == EOF)
      quit("Corrupted GIF transform: input is truncated.");
    --remaining;
    return static_cast<uint8_t>(value);
  };

  const uint32_t headerSize =
    (static_cast<uint32_t>(readRequiredByte()) << 8) | readRequiredByte();
  const int bsize = 255 - static_cast<int>(readRequiredByte());
  const uint32_t archivedClear =
    (static_cast<uint32_t>(readRequiredByte()) << 8) | readRequiredByte();
  const int codesize = readRequiredByte();
  if (headerSize < 6 || headerSize > size ||
      (headerSize - 6) % 4 != 0 || bsize <= 0 || bsize > 255 ||
      codesize < 2 || codesize > 8)
    quit("Corrupted GIF transform: invalid frozen header.");

  const uint32_t diffCount = (headerSize - 6) / 4;
  const int clearPos = static_cast<int>(
    (UINT32_C(69631) - archivedClear) & UINT32_C(0xffff));
  const int clearCode = 1 << codesize;
  if (diffCount > 4096 || clearPos <= clearCode + 2 || clearPos >= 8191)
    quit("Corrupted GIF transform: invalid dictionary reset contract.");

  Array<int> diffPos(4096);
  for (uint32_t i = 0; i < diffCount; ++i) {
    uint32_t delta = static_cast<uint32_t>(readRequiredByte()) << 24;
    delta |= static_cast<uint32_t>(readRequiredByte()) << 16;
    delta |= static_cast<uint32_t>(readRequiredByte()) << 8;
    delta |= readRequiredByte();
    uint64_t absolute = delta;
    if (i != 0)
      absolute += static_cast<uint64_t>(diffPos[i - 1]);
    if (absolute > static_cast<uint64_t>(std::numeric_limits<int>::max()))
      quit("Corrupted GIF transform: difference position overflow.");
    diffPos[i] = static_cast<int>(absolute);
  }
  if (size - remaining != headerSize || remaining == 0 ||
      remaining > static_cast<uint64_t>(std::numeric_limits<int>::max()))
    quit("Corrupted GIF transform: invalid transformed body length.");

  const uint64_t bodyLength = remaining;
  for (uint32_t i = 0; i < diffCount; ++i) {
    if (static_cast<uint64_t>(diffPos[i]) > bodyLength)
      quit("Corrupted GIF transform: difference position exceeds the body.");
  }

  int bits = codesize + 1;
  int shift = 0;
  uint32_t buffer = 0;
  int blockSize = 0;
  int maxcode = clearCode + 1;
  int input = 0;
  int code = 0;
  int offset = 0;
  int curDiff = 0;
  Array<int> dict(4096);
  Array<int> table(LZW_TABLE_SIZE);
  Array<uint8_t> output(256);
  LZW_RESET

  uint64_t outsize = 1;
  const auto writeBlock = [&](int count) -> bool {
    if (count < 0 || count > 255 ||
        outsize > UINT64_MAX - static_cast<uint64_t>(count + 1))
      quit("Corrupted GIF transform: output block length overflow.");
    output[0] = static_cast<uint8_t>(count);
    if (mode == FMode::FDECOMPRESS) {
      out->blockWrite(&output[0], static_cast<uint64_t>(count + 1));
    }
    else if (mode == FMode::FCOMPARE) {
      for (int j = 0; j < count + 1; ++j) {
        if (output[j] != out->getchar() && diffFound == 0) {
          diffFound = outsize + static_cast<uint64_t>(j) + 1;
        }
      }
    }
    outsize += static_cast<uint64_t>(count + 1);
    blockSize = 0;
    return true;
  };

  const auto writeCode = [&](int value) -> bool {
    if (value < 0 || value > 4095 || bits < 1 || bits > 12 ||
        shift < 0 || shift >= 8)
      quit("Corrupted GIF transform: invalid LZW code state.");
    buffer |= static_cast<uint32_t>(value) << shift;
    shift += bits;
    while (shift >= 8) {
      if (blockSize >= 255)
        quit("Corrupted GIF transform: output block exceeds 255 bytes.");
      output[++blockSize] = static_cast<uint8_t>(buffer);
      buffer >>= 8;
      shift -= 8;
      if (blockSize == bsize && !writeBlock(bsize))
        return false;
    }
    return true;
  };

  int last = readRequiredByte();
  if (mode == FMode::FDECOMPRESS) {
    out->putChar(static_cast<uint8_t>(codesize));
  }
  else if (mode == FMode::FCOMPARE &&
           codesize != out->getchar() && diffFound == 0) {
    diffFound = 1;
  }

  if (diffCount == 0 || diffPos[0] != 0) {
    if (!writeCode(clearCode))
      return 1;
  }
  else {
    ++curDiff;
  }

  while (remaining != 0) {
    input = readRequiredByte();
    if (last < 0 || last > 4095)
      quit("Corrupted GIF transform: invalid dictionary prefix.");
    const int key = static_cast<int>(
      (static_cast<uint32_t>(last) << 8) + static_cast<uint32_t>(input));
    int index = (code = -1);
    if (last < 0) {
      index = input;
    }
    else LZW_FIND(key)
    code = index;
    const uint64_t consumed = bodyLength - remaining;
    if (curDiff < static_cast<int>(diffCount) &&
        consumed > static_cast<uint64_t>(diffPos[curDiff])) {
      ++curDiff;
      code = -1;
    }
    if (code < 0) {
      if (!writeCode(last))
        return 1;
      if (maxcode == clearPos) {
        if (!writeCode(clearCode))
          return 1;
        bits = codesize + 1;
        maxcode = clearCode + 1;
        LZW_RESET
      }
      else {
        ++maxcode;
        if (maxcode >= 8191)
          quit("Corrupted GIF transform: dictionary growth exceeds its limit.");
        if (maxcode <= 4095) {
          dict[maxcode] = key;
          table[(index < 0) ? -index - 1 : offset] = maxcode;
        }
        if (maxcode >= (1 << bits) && bits < 12)
          ++bits;
      }
      code = input;
    }
    last = code;
  }

  if (!writeCode(last) || !writeCode(clearCode + 1))
    return 1;
  if (shift > 0) {
    if (blockSize >= 255)
      quit("Corrupted GIF transform: output block exceeds 255 bytes.");
    output[++blockSize] = static_cast<uint8_t>(buffer);
    if (blockSize == bsize && !writeBlock(bsize))
      return 1;
  }
  if (blockSize > 0 && !writeBlock(blockSize))
    return 1;

  if (outsize == UINT64_MAX)
    quit("Corrupted GIF transform: decoded length overflow.");
  if (mode == FMode::FDECOMPRESS) {
    out->putChar(0);
  }
  else if (mode == FMode::FCOMPARE &&
           0 != out->getchar() && diffFound == 0) {
    diffFound = outsize + 1;
  }
  return outsize + 1;
}
