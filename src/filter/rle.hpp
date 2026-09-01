#pragma once

#include "Filter.hpp"
#include "../VLI.hpp"

#include <cstddef>
#include <cstdint>

class RleFilter : Filter {
private:

  static bool readCanonicalVli(File* in, uint64_t inputSize,
                               uint64_t& bytesRead, uint64_t& value) {
    bytesRead = 0;
    value = 0;
    for (unsigned index = 0; index < 10; ++index) {
      if (bytesRead >= inputSize)
        return false;
      const int next = in->getchar();
      if (next == EOF)
        return false;
      const uint8_t byte = static_cast<uint8_t>(next);
      ++bytesRead;

      // A uint64_t VLI has at most one payload bit in its tenth byte.
      if (index == 9) {
        if ((byte & 0x80u) != 0 || (byte & 0x7fu) > 1)
          return false;
        value |= static_cast<uint64_t>(byte & 1u) << 63;
      }
      else {
        value |= static_cast<uint64_t>(byte & 0x7fu) << (index * 7);
      }

      if ((byte & 0x80u) == 0) {
        // putVLI() always uses the shortest representation.
        return index == 0 || (byte & 0x7fu) != 0;
      }
    }
    return false;
  }

  int scanLineSize = 0;

  enum class RleState {
      BASE, LITERAL, RUN, LITERAL_RUN
  } state = RleState::BASE;

  void rleOutputRun(uint8_t byte, uint8_t* &outPtr, int &run) {
    while (run > 128) {
        *outPtr++ = 0xFF, *outPtr++ = byte;
        run -= 128;
    }
      *outPtr++ = (uint8_t)(0x80 | (run - 1)), *outPtr++ = byte;
  }


  void handleRun(uint8_t byte, uint8_t *&outPtr, uint8_t *&lastLiteral, int &run) {
    if( run > 1 ) {
      state = RleState::RUN;
      rleOutputRun(byte, outPtr, run);
    } else {
      lastLiteral = outPtr;
      *outPtr++ = 0, *outPtr++ = byte;
      state = RleState::LITERAL;
    }
  }

  void handleLiteral(uint8_t byte, uint8_t *&outPtr, uint8_t *lastLiteral, int &run) {
    if( run > 1 ) {
      state = RleState::LITERAL_RUN;
      rleOutputRun(byte, outPtr, run);
    } else {
      if( ++(*lastLiteral) == 127 ) {
        state = RleState::BASE;
      }
      *outPtr++ = byte;
    }
  }

  uint8_t handleLiteralRun(uint8_t *outPtr, uint8_t *lastLiteral) {
    uint8_t loop = 0;
    if( outPtr[-2] == 0x81 && *lastLiteral < (125)) {
      state = (((*lastLiteral) += 2) == 127) ? RleState::BASE : RleState::LITERAL;
      outPtr[-2] = outPtr[-1];
    } else {
      state = RleState::RUN;
    }
    loop = 1;
    return loop;
  }

public:

  void setScanLineSize(int scanLineSize) {
    this->scanLineSize = scanLineSize; // Run-length Packets should never encode pixels from more than one scan line (important for "decode")
  }

  void encode(File *in, File *out, uint64_t size, int /*info*/, int& headerSize) override {
    uint8_t b = 0;
    uint8_t c = in->getchar();
    int i = 1;
    out->putVLI(scanLineSize);
    headerSize = VLICost(scanLineSize);

    while( i < static_cast<int>(size)) {
      b = in->getchar(), i++;
      if( c == 0x80 ) {
        c = b;
        continue;
      }
      if( c > 0x7F ) {
        for( uint32_t j = 0; j <= (c & 0x7F); j++ ) {
          out->putChar(b);
        }
        c = in->getchar(), i++;
      } else {
        for(uint32_t j = 0; j <= c; j++, i++ ) {
          out->putChar(b), b = in->getchar();
        }
        c = b;
      }
    }
  }

  uint64_t decode(File *in, File *out, FMode fMode, uint64_t size, uint64_t &diffFound) override {
    uint8_t inBuffer[0x10000] = {0};
    uint64_t pos = 0;
    uint64_t vliBytes = 0;
    uint64_t archivedScanLineSize = 0;
    if (!readCanonicalVli(in, size, vliBytes, archivedScanLineSize) ||
        archivedScanLineSize == 0 || archivedScanLineSize > sizeof(inBuffer))
      quit("Corrupted RLE transform: invalid scan-line size.");
    scanLineSize = static_cast<int>(archivedScanLineSize);
    if (vliBytes == size)
      quit("Corrupted RLE transform: missing scan-line data.");

    // In the worst case each source byte starts a two-byte run packet.  The
    // former fixed 0x10200-byte buffer was smaller than that proven bound.
    Array<uint8_t, 1> outBuffer(archivedScanLineSize * 2 + 2);
    uint64_t inputRemaining = size - vliBytes;

    while (inputRemaining != 0) {
      const uint64_t request = inputRemaining < archivedScanLineSize
        ? inputRemaining : archivedScanLineSize;
      uint64_t remaining = in->blockRead(&inBuffer[0], request);
      if (remaining != request)
        quit("Corrupted RLE transform: truncated scan-line data.");
      inputRemaining -= remaining;
      uint8_t *inPtr = (uint8_t *) inBuffer;
      uint8_t *outPtr = &outBuffer[0];
      uint8_t *lastLiteral = nullptr;
      state = RleState::BASE;
      while( remaining > 0 ) {
        uint8_t byte = *inPtr++;
        uint8_t loop = 0;
        int run = 1;
        for( remaining--; remaining > 0 && byte == *inPtr; remaining--, run++, inPtr++ ) {
        }
        do {
          loop = 0;
          switch( state ) {
            case RleState::BASE:
            case RleState::RUN: {
              handleRun(byte, outPtr, lastLiteral, run);
              break;
            }
            case RleState::LITERAL: {
              handleLiteral(byte, outPtr, lastLiteral, run);
              break;
            }
            case RleState::LITERAL_RUN: {
              loop = handleLiteralRun(outPtr, lastLiteral);
            }
          }
        } while( loop != 0 );
      }

      uint64_t length = static_cast<uint64_t>(outPtr - &outBuffer[0]);
      if (length > outBuffer.size())
        quit("Corrupted RLE transform: expanded scan line is too large.");
      if( fMode == FMode::FDECOMPRESS ) {
        out->blockWrite(&outBuffer[0], length);
      } else if( fMode == FMode::FCOMPARE ) {
        for(uint64_t j = 0; j < length; ++j ) {
          if( outBuffer[j] != out->getchar() && (diffFound == 0)) {
            diffFound = pos + j + 1;
          }
        }
      }
      pos += length;
    }
    return pos;
  }
};
