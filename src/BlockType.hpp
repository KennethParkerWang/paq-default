#pragma once

#include <type_traits>

enum class BlockType {
  DEFAULT = 0,
  JPEG,
  HDR,
  IMAGE1,
  IMAGE4,
  IMAGE8,
  IMAGE8GRAY,
  IMAGE24,
  IMAGE32,
  AUDIO,
  AUDIO_LE,
  EXE,
  CD,
  ZLIB,
  BASE64,
  GIF,
  PNG8,
  PNG8GRAY,
  PNG24,
  PNG32,
  TEXT,
  TEXT_EOL,
  RLE,
  LZW,
  DEC_ALPHA,
  MRB,
  DBF,
  BASE85,
  TAR,
  TARHDR,
  // Structured DEFAULT extensions. Keep these after every v216 value so the
  // legacy numeric identifiers above remain stable inside this derived format.
  RECORD,
  NUMERIC,
  WIDE_TEXT,
  Count
};

static_assert(static_cast<int>(BlockType::TARHDR) == 29, "v216 BlockType values must remain unchanged");
static_assert(static_cast<int>(BlockType::RECORD) == 30, "structured BlockType values must be appended");
static_assert(static_cast<int>(BlockType::Count) <= 256, "BlockType is stored in one byte");

inline int operator << (BlockType bt, int shift)
{
  using T = std::underlying_type_t <BlockType>;
  return (static_cast<T>(bt) << shift);
}

bool hasRecursion(BlockType ft);

bool hasInfo(BlockType ft);

bool hasTransform(BlockType ft, int info);

bool isPNG(BlockType ft);

bool isTEXT(BlockType ft);
