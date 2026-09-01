#pragma once

#include "Filters.hpp"
#include "../Utils.hpp"
#include <zlib.h>

static int parseZlibHeader(int header) {
  switch( header ) {
    case 0x2815:
      return 0;
    case 0x2853:
      return 1;
    case 0x2891:
      return 2;
    case 0x28cf:
      return 3;
    case 0x3811:
      return 4;
    case 0x384f:
      return 5;
    case 0x388d:
      return 6;
    case 0x38cb:
      return 7;
    case 0x480d:
      return 8;
    case 0x484b:
      return 9;
    case 0x4889:
      return 10;
    case 0x48c7:
      return 11;
    case 0x5809:
      return 12;
    case 0x5847:
      return 13;
    case 0x5885:
      return 14;
    case 0x58c3:
      return 15;
    case 0x6805:
      return 16;
    case 0x6843:
      return 17;
    case 0x6881:
      return 18;
    case 0x68de:
      return 19;
    case 0x7801:
      return 20;
    case 0x785e:
      return 21;
    case 0x789c:
      return 22;
    case 0x78da:
      return 23;
    default:
      return -1;
  }
}

static int zlibInflateInit(z_streamp strm, int zh) {
  if( zh == -1 ) {
    return inflateInit2(strm, -MAX_WBITS);
  }
  return inflateInit(strm);
}

MTFList mtf(81);

static int encodeZlib(File *in, File *out, uint64_t len, int &headerSize) {
  const int block = 1u << 16;
  const int limit = 128;
  uint8_t zin[block * 2];
  uint8_t zOut[block];
  uint8_t zRec[block * 2];
  uint8_t diffByte[81 * limit];
  uint64_t diffPos[81 * limit];

  // Step 1 - parse offset type form zlib stream header
  uint64_t posBackup = in->curPos();
  uint32_t h1 = in->getchar();
  uint32_t h2 = in->getchar();
  in->setpos(posBackup);
  int zh = parseZlibHeader(h1 * 256 + h2);
  int memLevel = 0;
  int cLevel = 0;
  int cType = zh % 4;
  int window = zh == -1 ? 0 : MAX_WBITS + 10 + zh / 4;
  int minCLevel = window == 0 ? 1 : cType == 3 ? 7 : cType == 2 ? 6 : cType == 1 ? 2 : 1;
  int maxCLevel = window == 0 ? 9 : cType == 3 ? 9 : cType == 2 ? 6 : cType == 1 ? 5 : 1;
  int index = -1;
  int nTrials = 0;
  bool found = false;

  // Step 2 - check recompressibility, determine parameters and save differences
  z_stream mainStrm;
  z_stream recStrm[81];
  int diffCount[81];
  int recPos[81];
  int mainRet = Z_STREAM_END;
  mainStrm.zalloc = Z_NULL;
  mainStrm.zfree = Z_NULL;
  mainStrm.opaque = Z_NULL;
  mainStrm.next_in = Z_NULL;
  mainStrm.avail_in = 0;
  if( zlibInflateInit(&mainStrm, zh) != Z_OK ) {
    return 0;
  }
  for( int i = 0; i < 81; i++ ) {
    cLevel = (i / 9) + 1;
    // Early skip if invalid parameter
    if( cLevel < minCLevel || cLevel > maxCLevel ) {
      diffCount[i] = limit;
      continue;
    }
    memLevel = (i % 9) + 1;
    recStrm[i].zalloc = Z_NULL;
    recStrm[i].zfree = Z_NULL;
    recStrm[i].opaque = Z_NULL;
    recStrm[i].next_in = Z_NULL;
    recStrm[i].avail_in = 0;
    int ret = deflateInit2(&recStrm[i], cLevel, Z_DEFLATED, window - MAX_WBITS, memLevel, Z_DEFAULT_STRATEGY);
    diffCount[i] = (ret == Z_OK) ? 0 : limit;
    recPos[i] = block * 2;
    diffPos[i * limit] = 0xFFFFFFFFFFFFFFFF;
    diffByte[i * limit] = 0;
  }

  for( uint64_t i = 0; i < len; i += block ) {
    uint32_t blSize = min(uint32_t(len - i), block);
    nTrials = 0;
    for( int j = 0; j < 81; j++ ) {
      if( diffCount[j] >= limit ) {
        continue;
      }
      nTrials++;
      if( recPos[j] >= block ) {
        recPos[j] -= block;
      }
    }
    // early break if nothing left to test
    if( nTrials == 0 ) {
      break;
    }
    memmove(&zRec[0], &zRec[block], block);
    memmove(&zin[0], &zin[block], block);
    in->blockRead(&zin[block], blSize); // Read block from input file

    // Decompress/inflate block
    mainStrm.next_in = &zin[block];
    mainStrm.avail_in = blSize;
    do {
      mainStrm.next_out = &zOut[0];
      mainStrm.avail_out = block;
      mainRet = inflate(&mainStrm, Z_FINISH);
      nTrials = 0;

      // Recompress/deflate block with all possible parameters
      for( int j = mtf.getFirst(); j >= 0; j = mtf.getNext()) {
        if( diffCount[j] >= limit ) {
          continue;
        }
        nTrials++;
        recStrm[j].next_in = &zOut[0];
        recStrm[j].avail_in = block - mainStrm.avail_out;
        recStrm[j].next_out = &zRec[recPos[j]];
        recStrm[j].avail_out = block * 2 - recPos[j];
        int ret = deflate(&recStrm[j], mainStrm.total_in == len ? Z_FINISH : Z_NO_FLUSH);
        if( ret != Z_BUF_ERROR && ret != Z_STREAM_END && ret != Z_OK ) {
          diffCount[j] = limit;
          continue;
        }

        // Compare
        int end = 2 * block - static_cast<int>(recStrm[j].avail_out);
        int tail = max(mainRet == Z_STREAM_END ? static_cast<int>(len) - static_cast<int>(recStrm[j].total_out) : 0, 0);
        for( int k = recPos[j]; k < end + tail; k++ ) {
          if((k < end && i + k - block < len && zRec[k] != zin[k]) || k >= end ) {
            if( ++diffCount[j] < limit ) {
              const int p = j * limit + diffCount[j];
              diffPos[p] = i + k - block;
              assert(k < int(sizeof(zin) / sizeof(*zin)));
              diffByte[p] = zin[k];
            }
          }
        }
        // Early break on perfect match
        if( mainRet == Z_STREAM_END && diffCount[j] == 0 ) {
          index = j;
          found = true;
          break;
        }
        recPos[j] = 2U * block - recStrm[j].avail_out;
      }
    } while( mainStrm.avail_out == 0 && mainRet == Z_BUF_ERROR && nTrials > 0 );
    if((mainRet != Z_BUF_ERROR && mainRet != Z_STREAM_END) || nTrials == 0 ) {
      break;
    }
  }
  int minCount = (found) ? 0 : limit;
  for( int i = 80; i >= 0; i-- ) {
    cLevel = (i / 9) + 1;
    if( cLevel >= minCLevel && cLevel <= maxCLevel ) {
      deflateEnd(&recStrm[i]);
    }
    if( !found && diffCount[i] < minCount ) {
      minCount = diffCount[index = i];
    }
  }
  inflateEnd(&mainStrm);
  if( minCount == limit ) {
    return 0;
  }
  mtf.moveToFront(index);

  // Step 3 - write parameters, differences and precompressed (inflated) data
  out->putChar(diffCount[index]);
  out->putChar(window);
  out->putChar(index);
  for( int i = 0; i <= diffCount[index]; i++ ) {
    const int v = i == diffCount[index] ? int(len - diffPos[index * limit + i]) :
                  int(diffPos[index * limit + i + 1] - diffPos[index * limit + i]) - 1;
    out->put32(v);
  }
  for( int i = 0; i < diffCount[index]; i++ ) {
    out->putChar(diffByte[index * limit + i + 1]);
  }

  in->setpos(posBackup);
  mainStrm.zalloc = Z_NULL;
  mainStrm.zfree = Z_NULL;
  mainStrm.opaque = Z_NULL;
  mainStrm.next_in = Z_NULL;
  mainStrm.avail_in = 0;
  if( zlibInflateInit(&mainStrm, zh) != Z_OK ) {
    return 0;
  }
  for( uint64_t i = 0; i < len; i += block ) {
    uint32_t blSize = min(uint32_t(len - i), block);
    in->blockRead(&zin[0], blSize);
    mainStrm.next_in = &zin[0];
    mainStrm.avail_in = blSize;
    do {
      mainStrm.next_out = &zOut[0];
      mainStrm.avail_out = block;
      mainRet = inflate(&mainStrm, Z_FINISH);
      out->blockWrite(&zOut[0], block - mainStrm.avail_out);
    } while( mainStrm.avail_out == 0 && mainRet == Z_BUF_ERROR);
    if( mainRet != Z_BUF_ERROR && mainRet != Z_STREAM_END ) {
      break;
    }
  }
  inflateEnd(&mainStrm);
  headerSize = diffCount[index] * 5 + 7;
  return static_cast<int>(mainRet == Z_STREAM_END);
}

static int decodeZlib(File *in, uint64_t size, File *out, FMode mode, uint64_t &diffFound) {
  const int block = 1u << 16;
  const int limit = 128;
  uint8_t zin[block];
  uint8_t zOut[block];
  if (in == nullptr || out == nullptr || size < 7)
    quit("Corrupted ZLIB transform header.");
  const int diffCount = in->getchar();
  const int archivedWindow = in->getchar();
  const int index = in->getchar();
  if (diffCount < 0 || diffCount >= limit || archivedWindow < 0 ||
      index < 0 || index >= 81 ||
      (archivedWindow != 0 &&
       (archivedWindow < MAX_WBITS + 10 ||
        archivedWindow > MAX_WBITS + 15)))
    quit("Corrupted ZLIB transform parameters.");
  const uint64_t headerSize = 7u + 5u * static_cast<uint64_t>(diffCount);
  if (headerSize > size)
    quit("Corrupted ZLIB transform header length.");
  const int window = archivedWindow - MAX_WBITS;
  int memLevel = (index % 9) + 1;
  int cLevel = (index / 9) + 1;
  int64_t restoredLength = 0;
  int64_t diffPos[limit];
  diffPos[0] = -1;
  for( int i = 0; i <= diffCount; i++ ) {
    const uint32_t archivedDelta = in->get32();
    if( i == diffCount ) {
      restoredLength = diffPos[i] + archivedDelta;
      if (restoredLength <= diffPos[i] || restoredLength > INT32_MAX)
        quit("Corrupted ZLIB restored length.");
    } else {
      const uint64_t candidate = static_cast<uint64_t>(diffPos[i] + 1) +
                                 archivedDelta;
      if (candidate > INT32_MAX)
        quit("Corrupted ZLIB difference position.");
      diffPos[i + 1] = static_cast<int64_t>(candidate);
    }
  }
  uint8_t diffByte[limit];
  diffByte[0] = 0;
  for( int i = 0; i < diffCount; i++ ) {
    const int c = in->getchar();
    if (c == EOF)
      quit("Corrupted ZLIB difference table.");
    diffByte[i + 1] = static_cast<uint8_t>(c);
  }
  size -= headerSize;

  z_stream recStrm;
  int diffIndex = 1;
  int64_t recPos = 0;
  recStrm.zalloc = Z_NULL;
  recStrm.zfree = Z_NULL;
  recStrm.opaque = Z_NULL;
  recStrm.next_in = Z_NULL;
  recStrm.avail_in = 0;
  int ret = deflateInit2(&recStrm, cLevel, Z_DEFLATED, window, memLevel, Z_DEFAULT_STRATEGY);
  if( ret != Z_OK ) {
    quit("Corrupted ZLIB transform cannot initialize its archived parameters.");
  }
  auto failAfterInit = [&](const char* message) {
    deflateEnd(&recStrm);
    quit(message);
  };
  // Execute at least one Z_FINISH call. A valid transformed payload may have
  // zero bytes when the original DEFLATE stream represents empty input, but
  // its zlib wrapper still has bytes that must be reconstructed.
  uint64_t inputOffset = 0;
  do {
    const uint32_t blSize = static_cast<uint32_t>(
      std::min<uint64_t>(size - inputOffset, block));
    if (blSize != 0 && in->blockRead(&zin[0], blSize) != blSize)
      failAfterInit("Corrupted ZLIB transform payload is truncated.");
    recStrm.next_in = blSize == 0 ? Z_NULL : &zin[0];
    recStrm.avail_in = blSize;
    const bool finalBlock = inputOffset + blSize == size;
    do {
      recStrm.next_out = &zOut[0];
      recStrm.avail_out = block;
      ret = deflate(&recStrm, finalBlock ? Z_FINISH : Z_NO_FLUSH);
      if( ret != Z_BUF_ERROR && ret != Z_STREAM_END && ret != Z_OK ) {
        failAfterInit("Corrupted ZLIB transform recompression failed.");
      }
      if (ret == Z_STREAM_END &&
          (!finalBlock || recStrm.avail_in != 0))
        failAfterInit("Corrupted ZLIB transform has trailing input.");
      const uint32_t produced = block - recStrm.avail_out;
      if (recPos > restoredLength)
        failAfterInit("Corrupted ZLIB transform exceeds its restored length.");
      // The frozen format stores the original compressed length. A selected
      // recompression candidate is allowed to produce a longer tail; legacy
      // encoding records differences only inside that original-length prefix.
      // Keep driving deflate to STREAM_END, but emit only the contractual
      // prefix rather than treating the unarchived tail as corruption.
      const uint32_t kept = static_cast<uint32_t>(std::min<uint64_t>(
        produced, static_cast<uint64_t>(restoredLength - recPos)));
      while( diffIndex <= diffCount && diffPos[diffIndex] >= recPos &&
             diffPos[diffIndex] < recPos + kept ) {
        zOut[static_cast<size_t>(diffPos[diffIndex] - recPos)] =
          diffByte[diffIndex];
        diffIndex++;
      }
      if( mode == FMode::FDECOMPRESS ) {
        out->blockWrite(&zOut[0], kept);
      } else if( mode == FMode::FCOMPARE ) {
        for( uint32_t j = 0; j < kept; j++ ) {
          if( zOut[j] != out->getchar() && (diffFound == 0)) {
            diffFound = recPos + j + 1;
          }
        }
      }
      recPos += kept;

    } while( recStrm.avail_out == 0 );
    if (recStrm.avail_in != 0)
      failAfterInit("Corrupted ZLIB transform input was not consumed.");
    inputOffset += blSize;
  } while (inputOffset < size);
  while( diffIndex <= diffCount ) {
    if (diffPos[diffIndex] != recPos || recPos >= restoredLength)
      failAfterInit("Corrupted ZLIB trailing difference positions.");
    if( mode == FMode::FDECOMPRESS ) {
      out->putChar(diffByte[diffIndex]);
    } else if( mode == FMode::FCOMPARE ) {
      if( diffByte[diffIndex] != out->getchar() && (diffFound == 0)) {
        diffFound = recPos + 1;
      }
    }
    diffIndex++;
    recPos++;
  }
  if (ret != Z_STREAM_END || recPos != restoredLength)
    failAfterInit("Corrupted ZLIB transform ended at the wrong length.");
  deflateEnd(&recStrm);
  return static_cast<int>(restoredLength);
}
