#pragma once

#include "DecAlpha.hpp"
#include "../file/File.hpp"
#include "../Encoder.hpp"
#include "Filter.hpp"
#include <cstdint>

class DECAlphaFilter : public Filter {
private:
  constexpr static uint64_t block = 0x10000; /**< block size */
public:
  /**
   * @todo Large file support
   * @param in
   * @param out
   * @param size
   * @param info
   */
  void encode(File* in, File* out, uint64_t size, int /*info*/,
              int& /*headerSize*/) override {
    if (in == nullptr || out == nullptr)
      quit("DEC Alpha transform input or output is unavailable.");
    Array<uint8_t> blk(block);
    for (uint64_t offset = 0; offset < size; offset += block) {
      const uint64_t length = std::min<uint64_t>(size - offset, block);
      const uint64_t bytesRead = in->blockRead(&blk[0], length);
      if (bytesRead != length)
        quit("encodeDECAlpha read error");
      for (size_t i = 0; i + 3 < static_cast<size_t>(bytesRead); i += 4) {
        uint32_t instruction = static_cast<uint32_t>(blk[i]) |
          (static_cast<uint32_t>(blk[i + 1]) << 8) |
          (static_cast<uint32_t>(blk[i + 2]) << 16) |
          (static_cast<uint32_t>(blk[i + 3]) << 24);
        if ((instruction >> 21) == (0x34 << 5) + 26) { // bsr r26, offset
          uint32_t addr = instruction & 0x1FFFFF;
          addr += static_cast<uint32_t>(offset + i) / 4u;
          instruction &= ~0x1FFFFF;
          instruction |= addr & 0x1FFFFF;
        }
        DECAlpha::Shuffle(instruction);
        blk[i] = instruction;
        blk[i + 1] = instruction >> 8;
        blk[i + 2] = instruction >> 16;
        blk[i + 3] = instruction >> 24;
      }
      out->blockWrite(&blk[0], bytesRead);
      if (length < block)
        break;
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
  uint64_t decode(File* /*in*/, File* out, FMode fMode, uint64_t size, uint64_t& diffFound) override {
    if (encoder == nullptr || out == nullptr)
      quit("DEC Alpha decoder input or output is unavailable.");
    Array<uint8_t> blk(block);
    for (uint64_t offset = 0; offset < size; offset += block) {
      const uint64_t length = std::min<uint64_t>(size - offset, block);
      for (size_t i = 0; i + 3 < static_cast<size_t>(length); i += 4) {
        blk[i]     = encoder->decompressByte(encoder->predictorMain);
        blk[i + 1] = encoder->decompressByte(encoder->predictorMain);
        blk[i + 2] = encoder->decompressByte(encoder->predictorMain);
        blk[i + 3] = encoder->decompressByte(encoder->predictorMain);
        uint32_t instruction = static_cast<uint32_t>(blk[i]) |
          (static_cast<uint32_t>(blk[i + 1]) << 8) |
          (static_cast<uint32_t>(blk[i + 2]) << 16) |
          (static_cast<uint32_t>(blk[i + 3]) << 24);
        DECAlpha::Unshuffle(instruction);
        if ((instruction >> 21) == (0x34 << 5) + 26) { // bsr r26, offset
          uint32_t addr = instruction & 0x1FFFFF;
          addr -= static_cast<uint32_t>(offset + i) / 4u;
          instruction &= ~0x1FFFFF;
          instruction |= addr & 0x1FFFFF;
        }
        blk[i] = instruction;
        blk[i + 1] = instruction >> 8;
        blk[i + 2] = instruction >> 16;
        blk[i + 3] = instruction >> 24;
      }
      size_t const l = static_cast<size_t>(length - (length & 3));
      for (size_t i = 0; i < static_cast<size_t>(length & 3); i++)
        blk[l + i] = encoder->decompressByte(encoder->predictorMain);

      if (fMode == FMode::FDECOMPRESS) {
        out->blockWrite(&blk[0], length);
        encoder->printStatus();
      }
      else if (fMode == FMode::FCOMPARE) {
        for (size_t i = 0; i < static_cast<size_t>(length); i++) {
          const int expected = out->getchar();
          if (blk[i] != expected && diffFound == 0)
            diffFound = offset + i + 1;
        }
      }
      if (length < block)
        break;
    }
    return size;
  }
};
