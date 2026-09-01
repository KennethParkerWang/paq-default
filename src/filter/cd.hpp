#pragma once

#include "Filter.hpp"
#include "ecc.hpp"

/**
 * @todo Large file support
 */
class CdFilter : Filter {
public:
  static int expandCdSector(uint8_t *data, int address, int test) {
    if (data == nullptr)
      quit("CD sector buffer is unavailable.");
    uint8_t d2[2352];
    eccedcInit();
    //sync pattern: 00 FF FF FF FF FF FF FF FF FF FF 00
    d2[0] = d2[11] = 0;
    for( int i = 1; i < 11; i++ ) {
      d2[i] = 255;
    }
    //determine Mode and Form
    int fMode = (data[15] != 1 ? 2 : 1);
    int form = (data[15] == 3 ? 2 : 1);
    //address (Minutes, Seconds, Sectors)
    if( address == -1 ) {
      for( int i = 12; i < 15; i++ ) {
        d2[i] = data[i];
      }
    } else {
      int c1 = (address & 15) + ((address >> 4) & 15) * 10;
      int c2 = ((address >> 8) & 15) + ((address >> 12) & 15) * 10;
      int c3 = ((address >> 16) & 15) + ((address >> 20) & 15) * 10;
      c1 = (c1 + 1) % 75;
      if( c1 == 0 ) {
        c2 = (c2 + 1) % 60;
        if( c2 == 0 ) {
          c3++;
        }
      }
      d2[12] = (c3 % 10) + 16 * (c3 / 10);
      d2[13] = (c2 % 10) + 16 * (c2 / 10);
      d2[14] = (c1 % 10) + 16 * (c1 / 10);
    }
    d2[15] = fMode;
    if( fMode == 2 ) {
      for( int i = 16; i < 24; i++ ) {
        d2[i] = data[i - 4 * static_cast<int>(i >= 20)]; //8 byte subheader
      }
    }
    if( form == 1 ) {
      if( fMode == 2 ) {
        d2[1] = d2[12], d2[2] = d2[13], d2[3] = d2[14];
        d2[12] = d2[13] = d2[14] = d2[15] = 0;
      } else {
        for( int i = 2068; i < 2076; i++ ) {
          d2[i] = 0; //Mode1: reserved 8 (zero) bytes
        }
      }
      for( int i = 16 + 8 * static_cast<int>(fMode == 2); i < 2064 + 8 * static_cast<int>(fMode == 2); i++ ) {
        d2[i] = data[i]; //data bytes
      }
      uint32_t edc = edcCompute(d2 + 16 * static_cast<int>(fMode == 2), 2064 - 8 * static_cast<int>(fMode == 2));
      for( int i = 0; i < 4; i++ ) {
        d2[2064 + 8 * static_cast<int>(fMode == 2) + i] = (edc >> (8 * i)) & 0xff;
      }
      eccCompute(d2 + 12, 86, 24, 2, 86, d2 + 2076);
      eccCompute(d2 + 12, 52, 43, 86, 88, d2 + 2248);
      if( fMode == 2 ) {
        d2[12] = d2[1], d2[13] = d2[2], d2[14] = d2[3], d2[15] = 2;
        d2[1] = d2[2] = d2[3] = 255;
      }
    }
    for( int i = 0; i < 2352; i++ ) {
      if( test != 0 && d2[i] != data[i] ) {
        form = 2;
      }
    }
    if( form == 2 ) {
      for( int i = 24; i < 2348; i++ ) {
        d2[i] = data[i]; //data bytes
      }
      uint32_t edc = edcCompute(d2 + 16, 2332);
      for( int i = 0; i < 4; i++ ) {
        d2[2348 + i] = (edc >> (8 * i)) & 0xff; //EDC
      }
    }
    for( int i = 0; i < 2352; i++ ) {
      if( test != 0 && d2[i] != data[i] ) {
        return 0;
      }
      data[i] = d2[i];
    }
    return fMode + form - 1;
  }

  void encode(File *in, File *out, uint64_t size, int info, int & /*headerSize*/) override {
    const int block = 2352;
    uint8_t blk[block];
    if (in == nullptr || out == nullptr || info < 1 || info > 3)
      quit("Invalid CD transform input.");
    uint64_t blockResidual = size % block;
    assert(blockResidual < 65536);
    out->putChar((blockResidual >> 8) & 255);
    out->putChar(blockResidual & 255);
    for( uint64_t offset = 0; offset < size; offset += block ) {
      const uint64_t remaining = size - offset;
      if( remaining < static_cast<uint64_t>(block) ) { //residual
        if (in->blockRead(&blk[0], remaining) != remaining)
          quit("CD transform input is truncated.");
        out->blockWrite(&blk[0], size - offset);
        break;
      } else { //normal sector
        if (in->blockRead(&blk[0], block) != block)
          quit("CD transform input is truncated.");
        if( info == 3 ) {
          blk[15] = 3; //indicate Mode2/Form2
        }
        if (blk[15] < 1 || blk[15] > 3)
          quit("Detected CD sector has an invalid mode.");
        if( offset == 0 ) {
          out->blockWrite(&blk[12], 4 + 4 * static_cast<int>(blk[15] != 1)); //4-byte address + 4 bytes from the 8-byte subheader goes only to the first sector
        }
        out->blockWrite(&blk[16 + 8 * static_cast<int>(blk[15] != 1)],
                        2048 + 276 * static_cast<int>(info == 3)); //user data goes to all sectors
        if( remaining < static_cast<uint64_t>(block) * 2 && blk[15] != 1 ) {
          out->blockWrite(&blk[16], 4); //in Mode2 4 bytes from the 8-byte subheader goes after the last sector
        }
      }
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
  uint64_t decode(File *in, File *out, FMode fMode, uint64_t size, uint64_t &diffFound) override {
    const int block = 2352;
    uint8_t blk[block];
    if (in == nullptr || out == nullptr)
      quit("CD decoder input or output is unavailable.");
    if (size < 2)
      quit("Corrupted CD transform: missing residual header.");
    const auto readRequiredByte = [in]() -> uint8_t {
      const int value = in->getchar();
      if (value == EOF)
        quit("Corrupted CD transform: truncated input.");
      return static_cast<uint8_t>(value);
    };
    const auto readExact = [in](uint8_t* destination, uint64_t length) {
      if (length != 0 && in->blockRead(destination, length) != length)
        quit("Corrupted CD transform: truncated input.");
    };
    uint64_t i = 0; //*in position
    uint64_t nextBlockPos = 0;
    int address = -1;
    int dataSize = 0;
    const uint64_t residual =
      (static_cast<uint64_t>(readRequiredByte()) << 8) | readRequiredByte();
    if (residual >= static_cast<uint64_t>(block))
      quit("Corrupted CD transform: invalid residual length.");
    size -= 2;
    while( i < size ) {
      if( size - i == residual ) { //residual data after last sector
        readExact(blk, residual);
        if (nextBlockPos > UINT64_MAX - residual)
          quit("Corrupted CD transform: decoded length overflow.");
        if( fMode == FMode::FDECOMPRESS ) {
          out->blockWrite(blk, residual);
        } else if( fMode == FMode::FCOMPARE ) {
          for( int j = 0; j < static_cast<int>(residual); ++j ) {
            if( blk[j] != out->getchar() && (diffFound == 0)) {
              diffFound = nextBlockPos + j + 1;
            }
          }
        }
        return nextBlockPos + residual;
      }
      if( i == 0 ) { //first sector
        if (size - i < 4)
          quit("Corrupted CD transform: truncated sector header.");
        readExact(blk + 12, 4); //header (4 bytes) consisting of address (Minutes, Seconds, Sectors) and fMode (1 = Mode1, 2 = Mode2/Form1, 3 = Mode2/Form2)
        i += 4;
        if (blk[15] < 1 || blk[15] > 3)
          quit("Corrupted CD transform: invalid sector mode.");
        if( blk[15] != 1 ) {
          if (size - i < 4)
            quit("Corrupted CD transform: truncated Mode 2 subheader.");
          readExact(blk + 16, 4); //Mode2: 4 bytes from the read 8-byte subheader
          i += 4;
        }
        dataSize = 2048 + static_cast<int>(blk[15] == 3) * 276; //user data bytes: Mode1 and Mode2/Form1: 2048 (ECC is present) or Mode2/Form2: 2048+276=2324 bytes (ECC is not present)
      } else { //normal sector
        address = (blk[12] << 16) + (blk[13] << 8) + blk[14]; //3-byte address (Minutes, Seconds, Sectors)
      }
      if (dataSize <= 0 || static_cast<uint64_t>(dataSize) > size - i)
        quit("Corrupted CD transform: truncated sector data.");
      readExact(blk + 16 + static_cast<int>(blk[15] != 1) * 8,
                static_cast<uint64_t>(dataSize)); //read data bytes, but skip 8-byte subheader in Mode 2 (which we processed already above)
      i += static_cast<uint64_t>(dataSize);
      if( dataSize > 2048 ) {
        blk[15] = 3; //indicate Mode2/Form2
      }
      if (residual > size - i)
        quit("Corrupted CD transform: residual exceeds remaining input.");
      if( blk[15] != 1 && size - i - residual == 4 ) { //Mode 2: we are at the last sector - grab the 4 subheader bytes
        readExact(blk + 16, 4);
        i += 4;
      }
      expandCdSector(blk, address, 0);
      if( fMode == FMode::FDECOMPRESS ) {
        out->blockWrite(blk, block);
      } else if( fMode == FMode::FCOMPARE ) {
        for( int j = 0; j < block; ++j ) {
          if( blk[j] != out->getchar() && (diffFound == 0)) {
            diffFound = nextBlockPos + j + 1;
          }
        }
      }
      if (nextBlockPos > UINT64_MAX - static_cast<uint64_t>(block))
        quit("Corrupted CD transform: decoded length overflow.");
      nextBlockPos += block;
    }
    if (residual != 0)
      quit("Corrupted CD transform: declared residual is missing.");
    return nextBlockPos;
  }
};
