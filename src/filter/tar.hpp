#pragma once

#include "Filter.hpp"
#include "../file/File.hpp"
#include "../CharacterNames.hpp"

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <limits>

/**
 * UStar (Unix Standard TAR) detection and transformation
 * 
 */
class TarFilter : public Filter {
private:

  struct TARheader { // 512 bytes
    char name[100];       //   0 | file name
    char mode[8];         // 100 | file mode (permissions)
    char uid[8];          // 108 | owner user id (octal)
    char gid[8];          // 116 | owner group id (octal)
    char size[12];        // 124 | file size in bytes (octal)
    char mtime[12];       // 136 | last modification time in numeric Unix time format (octal)
    char chksum[8];       // 148 | sum of unsigned characters in header block, filled with spaces while calculating (octal)
    char typeflag;        // 156 | link flag / file type
    char linkname[100];   // 157 | name of linked file
    char magic[8];        // 257 | "ustar\000" in calgary.tar created by windows 10 and gnu tar
                          //       "ustar  \0" in mozilla, samba, xml or in calgary.tar created by total commander or gnu tar
    char uname[32];       // 265 | owner user name (string)
    char gname[32];       // 297 | owner group name (string)
    char devmajor[8];     // 329 | device major number
    char devminor[8];     // 337 | device minor number
    char prefix[155];     // 345 | filename prefix
    char padding[12];     // 500 | padding

    int oct2bin(const char* p, int size) {
      while (size > 0 && *p == SPACE) { //skip leading spaces
        ++p;
        --size;
      }
      int i = 0;
      while (size > 0) {
        if (*p == 0/* && size == 1*/) //the last char must be a \0
          break;
        if (*p == SPACE)
          break;
        if (*p < '0' || *p > '7')
          return -1; //fail
        const int digit = *p - '0';
        if (i > (INT_MAX - digit) / 8)
          return -1; // value is not representable by the decoder
        i *= 8;
        i += digit;
        ++p;
        --size;
      }
      return i;
    }

    int calculateChecksum() {
      const char* p = &name[0];
      constexpr int chksumOffset = offsetof(TARheader, chksum);
      constexpr int chksumSize = 8;
      int u = 0;
      for (int n = 0; n < sizeof(TARheader); ++n) {
        if (n < chksumOffset || n >= chksumOffset + chksumSize) //exluding the checksum bytes
          u += ((uint8_t*)p)[n];
        else
          u += SPACE; // emulate spaces in place of the checksum bytes 
      }
      return u;
    }

    // checksum format examples: 
    // "0012201\0" (as in calgary.tar as created by total commander)
    // "012201\0 " (as in samba or calgary.tar created by windows 10 or gnu tar)
    // " 12201\0 " (as in mozilla and xml)

    void clearChecksum() {
      //look up the terminating \0 and fill it's left side with either spaces or '0's to preserve 
      //information about the format (see the checksum format examples above).
      char filler = chksum[0] == SPACE ? SPACE : '0';
      int i = 0;
      for (; i + 1 < static_cast<int>(sizeof(tarh.chksum)); i++)
        if (chksum[i + 1] == 0) break;
      //now i points to the last digit of the checksum
      for (; i >= 0; i--)
        chksum[i] = filler;
    }

    void generateChecksum() {
      //look up the terminating \0 and fill it's left side with the checksum (octal)
      int checksum = calculateChecksum();
      int i = 0;
      for (; i + 1 < static_cast<int>(sizeof(tarh.chksum)); i++)
        if (chksum[i + 1] == 0) break;
      //now i points to the last digit of the checksum
      for (; i >= 0; i--) {
        chksum[i] = (checksum & 7) + '0';
        checksum >>= 3;
        if (checksum == 0)
          break;
      }
    }

    bool verifyChecksum() {
      return calculateChecksum() == oct2bin(&chksum[0], 8);
    }

    bool isEmptySector() {
      const char* p = &name[0];
      for (int n = 511; n >= 0; --n)
        if (p[n] != 0)
          return false;
      return true;
    }

  } tarh;

  Array<uint64_t> detectedSectorStartPositions{ 0 };
  Array<uint64_t> detectedFileStartPositions{ 0 };
  Array<uint64_t> detectedFileLengths{ 0 };
  uint64_t detectedEmptySectorCount{ 0 };

  static constexpr uint64_t kMaximumRestoredBlockBytes = UINT32_MAX;
  static constexpr size_t kDecodeCopyBufferBytes = 16u * 1024u;

  static bool readCanonicalVli(File* input, uint64_t endPosition,
                               uint64_t& value) {
    value = 0;
    for (unsigned index = 0; index < 10; ++index) {
      if (input->curPos() >= endPosition)
        return false;
      const int next = input->getchar();
      if (next == EOF)
        return false;
      const uint8_t byte = static_cast<uint8_t>(next);
      if (index == 9) {
        if ((byte & 0x80u) != 0 || (byte & 0x7fu) > 1)
          return false;
        value |= static_cast<uint64_t>(byte & 1u) << 63;
      }
      else {
        value |= static_cast<uint64_t>(byte & 0x7fu) << (index * 7);
      }
      if ((byte & 0x80u) == 0)
        return index == 0 || (byte & 0x7fu) != 0;
    }
    return false;
  }

  static bool checkedAddWithin(uint64_t value, uint64_t add,
                               uint64_t limit, uint64_t& result) {
    if (value > limit || add > limit - value)
      return false;
    result = value + add;
    return true;
  }

  //detect tar content
  //a tar file is: hdr+filecontent + hdr+filecontent + etc...
  //this function figures out where each file starts and how long they are
  //we ignore files in tar having garbage in the padding area
  bool process(File* in, uint64_t maxFilePos) {
    uint64_t sectorStartPos = this->detectedStartPos;
    while (true) {
      if (sectorStartPos == maxFilePos) {
        //no empty sectors at the end - that'll be ok
        //this usually happens when we ignore files in a tar with garbage in the padding area
        this->detectedEndPos = sectorStartPos;
        return true;
      }
      else if (sectorStartPos > maxFilePos)
        return false; //fail
      in->setpos(sectorStartPos);
      int bytesRead = in->blockRead((uint8_t*)&tarh, sizeof(tarh));
      if (bytesRead != sizeof(tarh)) {
        return false; //fail
      }
      if (tarh.isEmptySector()) {
        do {
          detectedEmptySectorCount++;
          sectorStartPos += sizeof(tarh);
          int bytesRead = in->blockRead((uint8_t*)&tarh, sizeof(tarh));
          if (bytesRead != sizeof(tarh))
            break;
        } while (tarh.isEmptySector());
        this->detectedEndPos = sectorStartPos;
        return true;
      }
      //verify if all fields look octal that should look octal
      if (!tarh.verifyChecksum())
        return false; //fail
      if (tarh.oct2bin(tarh.mode, sizeof(tarh.mode)) < 0)
        return false; //fail
      if (tarh.oct2bin(tarh.uid, sizeof(tarh.uid)) < 0)
        return false; //fail
      if (tarh.oct2bin(tarh.gid, sizeof(tarh.gid)) < 0)
        return false; //fail
      if (tarh.oct2bin(tarh.size, sizeof(tarh.size)) < 0)
        return false; //fail
      if (tarh.oct2bin(tarh.mtime, sizeof(tarh.mtime)) < 0)
        return false; //fail
      if (tarh.oct2bin(tarh.devmajor, sizeof(tarh.devmajor)) < 0)
        return false; //fail
      if (tarh.oct2bin(tarh.devminor, sizeof(tarh.devminor)) < 0)
        return false; //fail

      detectedSectorStartPositions.pushBack(sectorStartPos);

      int fileSize = tarh.oct2bin(tarh.size, sizeof(tarh.size));
      if (fileSize != 0) {
        //detect if file is properly padded
        int filePaddingSize = (512 - (fileSize & 511)) & 511;
        in->setpos(sectorStartPos + sizeof(TARheader) + fileSize);
        for (int i = 0; i < filePaddingSize; i++) {
          int c = in->getchar();
          if (c != 0) {
            if (detectedFileStartPositions.size() > 0) {
              this->detectedEndPos = sectorStartPos;
              return true; //accept what we have so far (files with proper padding)
            }
            return false; //fail, there is not properly padded files so far
          }
        }
        detectedFileStartPositions.pushBack(sectorStartPos + sizeof(TARheader));
        detectedFileLengths.pushBack(fileSize);
      }

      int sectorsToJump = (fileSize + 511) >> 9;
      sectorStartPos += sizeof(TARheader) * (sectorsToJump + 1);
    }
  }

  void Print() {
    for (size_t i = 0; i < detectedSectorStartPositions.size(); i++) {
      printf("tar sector position: %d\n", (int)detectedSectorStartPositions[i]);
    }
    for (size_t i = 0; i < detectedFileStartPositions.size(); i++) {
      printf("file position: %d, length: %d\n", (int)detectedFileStartPositions[i], (int)detectedFileLengths[i]);
    }
    printf("empty sectors: %d, length: %d\n", (int)detectedEmptySectorCount, (int)(detectedEmptySectorCount * sizeof(tarh)));
  }

public:
  uint64_t detectedStartPos{};
  uint64_t detectedEndPos{};

  bool detect(File* in, uint64_t maxFilePos) {
    uint64_t userNamePos = in->curPos();
    this->detectedStartPos = userNamePos - offsetof(TARheader, uname);
    return process(in, maxFilePos);
  }

  void encode(File *in, File *out, uint64_t size, int width, int & headerSize) override {
    this->detectedStartPos = in->curPos();
    uint64_t maxFilePos = this->detectedStartPos + size;
    bool success = process(in, maxFilePos);
    if (!success)
      quit("Internal error in TAR detection.");

    //for debugging
    //Print();

    out->putVLI(detectedSectorStartPositions.size());
    out->putVLI(detectedEmptySectorCount);

    for (size_t i = 0; i < detectedSectorStartPositions.size(); i++) {
      in->setpos(detectedSectorStartPositions[i]);
      int bytesRead = in->blockRead((uint8_t*)&tarh, sizeof(tarh));
      if (bytesRead != sizeof(tarh)) {
        quit("Internal error in TAR transformation.");
      }
      tarh.clearChecksum();
      out->blockWrite((uint8_t*)&tarh, sizeof(tarh));
    }

    Array<uint8_t, 1> fileData{0};
    for (size_t i = 0; i < detectedFileStartPositions.size(); i++) {
      fileData.resize(detectedFileLengths[i]);
      in->setpos(detectedFileStartPositions[i]);
      in->blockRead(&fileData[0], detectedFileLengths[i]);
      out->blockWrite(&fileData[0], detectedFileLengths[i]);
    }

    return;
  }

  uint64_t decode(File* in, File* out, FMode fMode, uint64_t size, uint64_t& diffFound) override {
    const uint64_t inputStart = in->curPos();
    if (inputStart > std::numeric_limits<uint64_t>::max() - size)
      quit("Corrupted TAR transform: input range overflows.");
    const uint64_t inputEnd = inputStart + size;

    uint64_t sectorCount64 = 0;
    uint64_t emptySectorCount = 0;
    if (!readCanonicalVli(in, inputEnd, sectorCount64) ||
        !readCanonicalVli(in, inputEnd, emptySectorCount))
      quit("Corrupted TAR transform: invalid count header.");
    const uint64_t headerStart = in->curPos();
    if (sectorCount64 > std::numeric_limits<size_t>::max() ||
        sectorCount64 > kMaximumRestoredBlockBytes / sizeof(TARheader) ||
        headerStart > inputEnd ||
        sectorCount64 > (inputEnd - headerStart) / sizeof(TARheader))
      quit("Corrupted TAR transform: header table is too large.");
    const size_t sectorCount = static_cast<size_t>(sectorCount64);
    const uint64_t headerBytes = sectorCount64 * sizeof(TARheader);
    uint64_t fileDataStartPos = headerStart + headerBytes;

    std::array<uint8_t, kDecodeCopyBufferBytes> fileData{};
    std::array<uint8_t, kDecodeCopyBufferBytes> zeroData{};
    uint64_t p = 0;
    for (size_t i = 0; i < sectorCount; i++) {
      in->setpos(headerStart + static_cast<uint64_t>(i) * sizeof(TARheader));
      if (in->blockRead(reinterpret_cast<uint8_t*>(&tarh), sizeof(tarh)) !=
          sizeof(tarh))
        quit("Corrupted TAR transform: truncated header table.");
      tarh.generateChecksum();
      const int parsedFileSize = tarh.oct2bin(tarh.size, sizeof(tarh.size));
      if (parsedFileSize < 0)
        quit("Corrupted TAR transform: invalid file size.");
      const uint64_t fileSize = static_cast<uint64_t>(parsedFileSize);

      uint64_t afterHeader = 0;
      if (!checkedAddWithin(p, sizeof(tarh), kMaximumRestoredBlockBytes,
                            afterHeader))
        quit("Corrupted TAR transform: restored block is too large.");

      if (fMode == FMode::FDECOMPRESS) {
        out->blockWrite(reinterpret_cast<uint8_t*>(&tarh), sizeof(tarh));
      }
      else if (fMode == FMode::FCOMPARE) {
        for (int j = 0; j < sizeof(tarh); j++) {
          int c1 = out->getchar();
          int c2 = reinterpret_cast<uint8_t*>(&tarh)[j];
          if (c1 != c2 && (diffFound == 0)) {
            diffFound = p + static_cast<uint64_t>(j) + 1;
          }
        }
      }
      p = afterHeader;

      if (fileSize != 0) {
        if (fileDataStartPos > inputEnd ||
            fileSize > inputEnd - fileDataStartPos)
          quit("Corrupted TAR transform: file data exceeds transformed input.");
        in->setpos(fileDataStartPos);
        uint64_t remainingFileBytes = fileSize;
        while (remainingFileBytes != 0) {
          const uint64_t request = remainingFileBytes < fileData.size()
            ? remainingFileBytes : fileData.size();
          if (in->blockRead(fileData.data(), request) != request)
            quit("Corrupted TAR transform: truncated file data.");
          if (fMode == FMode::FDECOMPRESS) {
            out->blockWrite(fileData.data(), request);
          }
          else if (fMode == FMode::FCOMPARE) {
            for (uint64_t j = 0; j < request; ++j) {
              if (fileData[static_cast<size_t>(j)] != out->getchar() &&
                  diffFound == 0)
                diffFound = p + j + 1;
            }
          }
          if (!checkedAddWithin(p, request, kMaximumRestoredBlockBytes, p))
            quit("Corrupted TAR transform: restored block is too large.");
          remainingFileBytes -= request;
        }
        fileDataStartPos += fileSize;

        uint64_t padding = (sizeof(tarh) - (fileSize & 511u)) & 511u;
        while (padding != 0) {
          const uint64_t request = padding < zeroData.size()
            ? padding : zeroData.size();
          if (fMode == FMode::FDECOMPRESS) {
            out->blockWrite(zeroData.data(), request);
          }
          else if (fMode == FMode::FCOMPARE) {
            for (uint64_t j = 0; j < request; ++j) {
              if (out->getchar() != 0 && diffFound == 0)
                diffFound = p + j + 1;
            }
          }
          if (!checkedAddWithin(p, request, kMaximumRestoredBlockBytes, p))
            quit("Corrupted TAR transform: restored block is too large.");
          padding -= request;
        }
      }
    }

    if (fileDataStartPos != inputEnd)
      quit("Corrupted TAR transform: unexpected trailing transformed data.");
    if (emptySectorCount >
        (kMaximumRestoredBlockBytes - p) / sizeof(tarh))
      quit("Corrupted TAR transform: empty-sector run is too large.");
    uint64_t emptyBytes = emptySectorCount * sizeof(tarh);
    while (emptyBytes != 0) {
      const uint64_t request = emptyBytes < zeroData.size()
        ? emptyBytes : zeroData.size();
      if (fMode == FMode::FDECOMPRESS) {
        out->blockWrite(zeroData.data(), request);
      }
      else if (fMode == FMode::FCOMPARE) {
        for (uint64_t j = 0; j < request; ++j) {
          if (out->getchar() != 0 && diffFound == 0)
            diffFound = p + j + 1;
        }
      }
      p += request; // Prevalidated against kMaximumRestoredBlockBytes above.
      emptyBytes -= request;
    }

    in->setpos(fileDataStartPos);

    return p;
  }

  void getFilePositions(File* in, Array<uint64_t,1> &filePositions) {
    assert(filePositions.size() == 0);
    const uint64_t sectorCount = in->getVLI();
    const uint64_t emptySectorCount = in->getVLI();
    const uint64_t curPos = in->curPos();
    if (sectorCount > (std::numeric_limits<uint64_t>::max() - curPos) /
                        sizeof(tarh))
      quit("Internal TAR planning header range overflow.");
    uint64_t fileDataStartPos = curPos + sectorCount * sizeof(tarh);
    filePositions.pushBack(fileDataStartPos); //the first entry is the first file position
    for (uint64_t i = 0; i < sectorCount; i++) {
      int bytesRead = in->blockRead((uint8_t*)&tarh, sizeof(tarh));
      if (bytesRead != sizeof(tarh))
        quit("Internal TAR planning header table is truncated.");

      int fileSize = tarh.oct2bin(tarh.size, 12);
      if (fileSize < 0)
        quit("Internal TAR planning file size is invalid.");

      if (fileSize != 0) {
        if (static_cast<uint64_t>(fileSize) >
            std::numeric_limits<uint64_t>::max() - fileDataStartPos)
          quit("Internal TAR planning file range overflow.");
        fileDataStartPos += fileSize;
        filePositions.pushBack(fileDataStartPos); //the last entry is exactly the tempfile size (it points past to the last file)
      }
    }
    (void)emptySectorCount; // Empty sectors are reconstructed, not stored.
  }

};
