#include <cstdint>
#include "BlockType.hpp"

bool hasRecursion(BlockType ft) {
  return ft == BlockType::CD || ft == BlockType::ZLIB || ft == BlockType::BASE64 || ft == BlockType::BASE85 || ft == BlockType::GIF || ft == BlockType::RLE || ft == BlockType::LZW || ft == BlockType::PNG8 || ft == BlockType::PNG8GRAY || ft == BlockType::PNG24 || ft == BlockType::PNG32 || ft == BlockType::TAR;
}

bool hasInfo(BlockType ft) {
  return ft == BlockType::IMAGE1 || ft == BlockType::IMAGE4 || ft == BlockType::IMAGE8 || ft == BlockType::IMAGE8GRAY || ft == BlockType::IMAGE24 || ft == BlockType::IMAGE32 || ft == BlockType::AUDIO ||
    ft == BlockType::AUDIO_LE || ft == BlockType::PNG8 || ft == BlockType::PNG8GRAY || ft == BlockType::PNG24 || ft == BlockType::PNG32 || ft == BlockType::MRB || ft == BlockType::DBF || ft == BlockType::EXE ||
    ft == BlockType::RECORD || ft == BlockType::NUMERIC || ft == BlockType::WIDE_TEXT;
}

bool hasTransform(BlockType ft, int info) {
  if (ft == BlockType::MRB) {
    uint8_t packingMethod = (info >> 24) & 3; //0..3
    return packingMethod != 0; //0: uncompressed, 1: rle encoded
  }
  if (ft == BlockType::RECORD) {
    // Mode 0 is a model-only stride hint. Invalid nonzero modes deliberately
    // enter transform dispatch, where metadata validation rejects the archive.
    return ((static_cast<uint32_t>(info) >> 12) & 3U) != 0;
  }
  if (ft == BlockType::NUMERIC || ft == BlockType::WIDE_TEXT)
    return true;
  return ft == BlockType::IMAGE24 || ft == BlockType::IMAGE32 || ft == BlockType::AUDIO_LE || ft == BlockType::EXE || ft == BlockType::CD || ft == BlockType::ZLIB || ft == BlockType::BASE64 || ft == BlockType::BASE85 || ft == BlockType::GIF ||
    ft == BlockType::TEXT_EOL || ft == BlockType::RLE || ft == BlockType::LZW || ft == BlockType::DEC_ALPHA || ft == BlockType::PNG8 || ft == BlockType::PNG8GRAY || ft == BlockType::PNG24 || ft == BlockType::PNG32 || ft == BlockType::TAR;
}

bool isPNG(BlockType ft) { return ft == BlockType::PNG8 || ft == BlockType::PNG8GRAY || ft == BlockType::PNG24 || ft == BlockType::PNG32; }

bool isTEXT(BlockType ft) { return ft == BlockType::TEXT || ft == BlockType::TEXT_EOL || ft == BlockType::DBF; }
