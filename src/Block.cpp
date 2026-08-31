#include "Block.hpp"

#include "Utils.hpp"
#include "filter/StructuredDataFilter.hpp"

namespace Block {

  void EncodeBlockHeader(Encoder* const encoder, BlockType blockType, uint64_t blockSize, int blockInfo) {
    if (structured::isStructuredType(blockType))
      structured::validateStructuredInfo(blockType, static_cast<uint32_t>(blockInfo));
    EncodeBlockType(encoder, blockType);
    EncodeBlockSize(encoder, blockSize);
    if (hasInfo(blockType))
      EncodeInfo(encoder, blockInfo);
    encoder->initContextForBlockModel(blockType, blockInfo);
  }

  uint64_t DecodeBlockHeader(Encoder* const encoder) { //returns blockSize
    BlockType blockType = DecodeBlockType(encoder);
    uint64_t blockSize = DecodeBlockSize(encoder);
    int blockInfo = -1;
    if (hasInfo(blockType))
      blockInfo = DecodeInfo(encoder);
    if (structured::isStructuredType(blockType))
      structured::validateStructuredInfo(blockType, static_cast<uint32_t>(blockInfo));

    Shared* shared = encoder->getShared();
    shared->State.blockType = blockType;
    shared->State.blockInfo = blockInfo;
    shared->State.blockPos = UINT32_MAX;
    return blockSize;
  }

  void EncodeBlockType(Encoder* const encoder, BlockType blocktype) {
    if (static_cast<unsigned>(blocktype) >= static_cast<unsigned>(BlockType::Count))
      quit("Internal error: invalid block type");
    encoder->setContextForBlockModel(0x10);
    encoder->compressByte(encoder->predictorBlock, uint8_t(blocktype));
    encoder->appendToBlockTypeHistoryForBlockModel(blocktype);
  }

  BlockType DecodeBlockType(Encoder* const encoder) {
    encoder->setContextForBlockModel(0x10);
    const uint8_t encodedType = encoder->decompressByte(encoder->predictorBlock);
    if (encodedType >= static_cast<uint8_t>(BlockType::Count))
      quit("Corrupted archive: invalid block type");
    const BlockType blockType = static_cast<BlockType>(encodedType);
    encoder->appendToBlockTypeHistoryForBlockModel(blockType);
    return blockType;
  }

  void EncodeBlockSize(Encoder* const encoder, uint64_t blockSize) {
    encoder->setContextForBlockModel(0x20);
    encoder->compressByte(encoder->predictorBlock, (blockSize >> 24) & 0xFF);
    encoder->setContextForBlockModel(0x21);
    encoder->compressByte(encoder->predictorBlock, (blockSize >> 16) & 0xFF);
    encoder->setContextForBlockModel(0x22);
    encoder->compressByte(encoder->predictorBlock, (blockSize >> 8) & 0xFF);
    encoder->setContextForBlockModel(0x23);
    encoder->compressByte(encoder->predictorBlock, (blockSize) & 0xFF);
  }

  uint64_t DecodeBlockSize(Encoder* const encoder) {
    uint64_t blockSize = 0;
    uint8_t b;
    encoder->setContextForBlockModel(0x20);
    b = encoder->decompressByte(encoder->predictorBlock);
    blockSize = blockSize << 8 | b;
    encoder->setContextForBlockModel(0x21);
    b = encoder->decompressByte(encoder->predictorBlock);
    blockSize = blockSize << 8 | b;
    encoder->setContextForBlockModel(0x22);
    b = encoder->decompressByte(encoder->predictorBlock);
    blockSize = blockSize << 8 | b;
    encoder->setContextForBlockModel(0x23);
    b = encoder->decompressByte(encoder->predictorBlock);
    blockSize = blockSize << 8 | b;
    return blockSize;
  }

  void EncodeInfo(Encoder* const encoder, int info) {
    encoder->setContextForBlockModel(0x30);
    encoder->compressByte(encoder->predictorBlock, (info >> 24) & 0xFF);
    encoder->setContextForBlockModel(0x31);
    encoder->compressByte(encoder->predictorBlock, (info >> 16) & 0xFF);
    encoder->setContextForBlockModel(0x32);
    encoder->compressByte(encoder->predictorBlock, (info >> 8) & 0xFF);
    encoder->setContextForBlockModel(0x33);
    encoder->compressByte(encoder->predictorBlock, (info) & 0xFF);
  }

  int DecodeInfo(Encoder* const encoder) {
    uint32_t info = 0;
    uint8_t b;
    encoder->setContextForBlockModel(0x30);
    b = encoder->decompressByte(encoder->predictorBlock);
    info = info << 8 | b;
    encoder->setContextForBlockModel(0x31);
    b = encoder->decompressByte(encoder->predictorBlock);
    info = info << 8 | b;
    encoder->setContextForBlockModel(0x32);
    b = encoder->decompressByte(encoder->predictorBlock);
    info = info << 8 | b;
    encoder->setContextForBlockModel(0x33);
    b = encoder->decompressByte(encoder->predictorBlock);
    info = info << 8 | b;
    return info;
  }


};
