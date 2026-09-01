#pragma once

#include "../file/File.hpp"
#include "../Block.hpp"
#include "../Encoder.hpp"
#include "Filter.hpp"
#include <cstdint>

/**
 * EXE transform: <encoded-size> <begin> <block>...
 * Encoded-size is 4 bytes, MSB first.
 * begin is the offset of the start of the input file, 4 bytes, MSB first.
 * Each block applies the e8e9 transform to strings falling entirely
 * within the block starting from the end and working backwards.
 * The 5 byte pattern is E8/E9 xx xx xx 00/FF (x86 CALL/JMP xxxxxxxx)
 * where xxxxxxxx is a relative address LSB first.  The address is
 * converted to an absolute address by adding the offset mod 2^25
 * (in range +-2^24).
 */
class ExeFilter : public Filter {
private:
  constexpr static int block = 0x10000; /**< block size */
  int info;

  static int32_t signExtend25(uint32_t value) {
    const uint32_t low25 = value & UINT32_C(0x01ffffff);
    return (low25 & UINT32_C(0x01000000)) != 0
      ? static_cast<int32_t>(low25) - INT32_C(0x02000000)
      : static_cast<int32_t>(low25);
  }
public:
  
void setBegin(int info) {
  this->info = info;
}

/**
    * @param in
    * @param out
    * @param size
    * @param info
    */
  void encode(File *in, File *out, uint64_t size, int info, int &/*headerSize*/) override {
    Array<uint8_t> blk(block);

    // Transform
    for( uint64_t offset = 0; offset < size; offset += block ) {
      uint32_t size1 = min(uint32_t(size - offset), block);
      int bytesRead = static_cast<int>(in->blockRead(&blk[0], size1));
      if( bytesRead != static_cast<int>(size1)) {
        quit("encodeExe read error");
      }
      for( int i = bytesRead - 1; i >= 5; --i ) {
        if((blk[i - 4] == 0xe8 || blk[i - 4] == 0xe9 || (blk[i - 5] == 0x0f && (blk[i - 4] & 0xf0) == 0x80)) &&
            (blk[i] == 0 || blk[i] == 0xff)) {
          const uint32_t relative =
            static_cast<uint32_t>(blk[i - 3]) |
            (static_cast<uint32_t>(blk[i - 2]) << 8) |
            (static_cast<uint32_t>(blk[i - 1]) << 16) |
            (static_cast<uint32_t>(blk[i]) << 24);
          const uint32_t location = static_cast<uint32_t>(offset) +
            static_cast<uint32_t>(info) + static_cast<uint32_t>(i + 1);
          const uint32_t absolute = static_cast<uint32_t>(
            signExtend25(relative + location));
          blk[i] = static_cast<uint8_t>(absolute >> 24);
          blk[i - 1] = static_cast<uint8_t>(absolute ^ 176u);
          blk[i - 2] = static_cast<uint8_t>((absolute >> 8) ^ 176u);
          blk[i - 3] = static_cast<uint8_t>((absolute >> 16) ^ 176u);
        }
      }
      out->blockWrite(&blk[0], bytesRead);
    }
  }

  /**
    * @todo Large file support
    * @param in
    * @param out
    * @param fMode
    * @param size
    * @param diffFound
    * @return
    */
  uint64_t decode(File */*in*/, File* out, FMode fMode, uint64_t size, uint64_t& diffFound) override {
    if (size > UINT64_MAX - 6)
      quit("Corrupted EXE transform length.");
    if (size < 5) {
      for (uint64_t offset = 0; offset < size; ++offset) {
        const uint8_t byte =
          encoder->decompressByte(encoder->predictorMain);
        if (fMode == FMode::FDECOMPRESS)
          out->putChar(byte);
        else if (fMode == FMode::FCOMPARE && byte != out->getchar() &&
                 diffFound == 0)
          diffFound = offset + 1;
      }
      return size;
    }
    uint64_t offset = 6;
    uint8_t c[6];
    const uint32_t begin = static_cast<uint32_t>(info);
    for( int i = 4; i >= 0; i-- ) {
      c[i] = encoder->decompressByte(encoder->predictorMain); // Fill queue
    }

    while( offset < size + 6 ) {
      memmove(c + 1, c, 5);
      if( offset <= size) {
        c[0] = encoder->decompressByte(encoder->predictorMain);
      }
      // E8E9 transform: E8/E9 xx xx xx 00/FF -> subtract location from x
      if((c[0] == 0x00 || c[0] == 0xFF) && (c[4] == 0xE8 || c[4] == 0xE9 || (c[5] == 0x0F && (c[4] & 0xF0) == 0x80)) &&
          ((((offset - 1) ^ (offset - 6)) &
            ~static_cast<uint64_t>(block - 1)) == 0) &&
          offset <= size) { // not crossing block boundary
        const uint32_t absolute =
          static_cast<uint32_t>(c[1] ^ 176u) |
          (static_cast<uint32_t>(c[2] ^ 176u) << 8) |
          (static_cast<uint32_t>(c[3] ^ 176u) << 16) |
          (static_cast<uint32_t>(c[0]) << 24);
        const uint32_t relative = static_cast<uint32_t>(signExtend25(
          absolute - static_cast<uint32_t>(offset) - begin));
        c[3] = static_cast<uint8_t>(relative);
        c[2] = static_cast<uint8_t>(relative >> 8);
        c[1] = static_cast<uint8_t>(relative >> 16);
        c[0] = static_cast<uint8_t>(relative >> 24);
      }
      if( fMode == FMode::FDECOMPRESS ) {
        out->putChar(c[5]);
      } else if( fMode == FMode::FCOMPARE && c[5] != out->getchar() && (diffFound == 0)) {
        diffFound = offset - 6 + 1;
      }
      if( fMode == FMode::FDECOMPRESS && ((offset & 0x0fff) == 0)) {
        encoder->printStatus();
      }
      offset++;
    }
    return size;
  }
};
