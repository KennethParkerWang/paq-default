#pragma once

#include "../Utils.hpp"
#include "../file/File.hpp"
#include "ProfileParameters.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace routed {

constexpr uint32_t kMaximumRecipeOperations = 2u * 1000u * 1000u;
constexpr uint32_t kMaximumLiteralBytesPerRecipe = 8u * 1024u * 1024u;

enum class RecipeOpcode : uint8_t {
  COPY_LITERAL = 0,
  COPY_SEGMENT_RANGE = 1,
  END = 0xff
};

struct RecipeOperation {
  RecipeOpcode opcode = RecipeOpcode::END;
  uint32_t segmentIndex = 0;
  uint64_t sourceOffset = 0;
  uint64_t length = 0;
  std::vector<uint8_t> literal;
};

struct ReconstructionRecipe {
  uint64_t outputLength = 0;
  std::vector<RecipeOperation> operations;
};

inline std::vector<uint8_t> encodeRecipe(const ReconstructionRecipe& recipe) {
  if (recipe.operations.size() > kMaximumRecipeOperations)
    quit("Reconstruction recipe contains too many operations.");
  CanonicalWriter writer;
  writer.putUleb128(recipe.outputLength);
  writer.putUleb128(recipe.operations.size());
  for (const RecipeOperation& operation : recipe.operations) {
    writer.putByte(static_cast<uint8_t>(operation.opcode));
    if (operation.opcode == RecipeOpcode::COPY_LITERAL) {
      if (operation.literal.size() > kMaximumLiteralBytesPerRecipe)
        quit("Reconstruction recipe literal exceeds its resource limit.");
      writer.putBlob(operation.literal);
    }
    else if (operation.opcode == RecipeOpcode::COPY_SEGMENT_RANGE) {
      writer.putUleb128(operation.segmentIndex);
      writer.putUleb128(operation.sourceOffset);
      writer.putUleb128(operation.length);
    }
    else {
      quit("Invalid reconstruction recipe operation.");
    }
  }
  writer.putByte(static_cast<uint8_t>(RecipeOpcode::END));
  return writer.take();
}

inline bool decodeRecipe(const std::vector<uint8_t>& bytes,
                         ReconstructionRecipe& recipe) {
  CanonicalReader reader(bytes);
  uint64_t operationCount = 0;
  if (!reader.readUleb128(recipe.outputLength) ||
      !reader.readUleb128(operationCount) ||
      operationCount > kMaximumRecipeOperations)
    return false;
  recipe.operations.clear();
  recipe.operations.reserve(static_cast<size_t>(operationCount));
  uint64_t produced = 0;
  for (uint64_t index = 0; index < operationCount; ++index) {
    uint8_t opcode = 0;
    if (!reader.readByte(opcode))
      return false;
    RecipeOperation operation;
    operation.opcode = static_cast<RecipeOpcode>(opcode);
    if (operation.opcode == RecipeOpcode::COPY_LITERAL) {
      if (!reader.readBlob(operation.literal, kMaximumLiteralBytesPerRecipe))
        return false;
      operation.length = operation.literal.size();
    }
    else if (operation.opcode == RecipeOpcode::COPY_SEGMENT_RANGE) {
      uint64_t segmentIndex = 0;
      if (!reader.readUleb128(segmentIndex) ||
          segmentIndex > std::numeric_limits<uint32_t>::max() ||
          !reader.readUleb128(operation.sourceOffset) ||
          !reader.readUleb128(operation.length))
        return false;
      operation.segmentIndex = static_cast<uint32_t>(segmentIndex);
    }
    else {
      return false;
    }
    if (operation.length > recipe.outputLength - produced)
      return false;
    produced += operation.length;
    recipe.operations.push_back(std::move(operation));
  }
  uint8_t endOpcode = 0;
  return produced == recipe.outputLength &&
         reader.readByte(endOpcode) &&
         endOpcode == static_cast<uint8_t>(RecipeOpcode::END) &&
         reader.atEnd();
}

inline void copyRecipeBytes(File* source, File* destination, uint64_t offset,
                            uint64_t length) {
  if (source == nullptr || destination == nullptr)
    quit("Reconstruction recipe source or destination is unavailable.");
  const uint64_t savedPosition = source->curPos();
  source->setpos(offset);
  std::array<uint8_t, 64u * 1024u> buffer{};
  uint64_t remaining = length;
  while (remaining != 0) {
    const uint64_t request = remaining < buffer.size() ? remaining : buffer.size();
    if (source->blockRead(buffer.data(), request) != request)
      quit("Reconstruction recipe segment range is truncated.");
    destination->blockWrite(buffer.data(), request);
    remaining -= request;
  }
  source->setpos(savedPosition);
}

inline void executeRecipe(const ReconstructionRecipe& recipe,
                          const std::vector<File*>& decodedSegments,
                          File* output) {
  if (output == nullptr)
    quit("Reconstruction recipe output is unavailable.");
  output->setEnd();
  if (output->curPos() != 0)
    quit("Reconstruction recipe output must initially be empty.");
  output->setpos(0);
  for (const RecipeOperation& operation : recipe.operations) {
    if (operation.opcode == RecipeOpcode::COPY_LITERAL) {
      if (!operation.literal.empty()) {
        output->blockWrite(const_cast<uint8_t*>(operation.literal.data()),
                           operation.literal.size());
      }
    }
    else if (operation.opcode == RecipeOpcode::COPY_SEGMENT_RANGE) {
      if (operation.segmentIndex >= decodedSegments.size())
        quit("Reconstruction recipe references an unknown segment.");
      copyRecipeBytes(decodedSegments[operation.segmentIndex], output,
                      operation.sourceOffset, operation.length);
    }
    else {
      quit("Invalid reconstruction recipe operation during execution.");
    }
  }
  output->setEnd();
  if (output->curPos() != recipe.outputLength)
    quit("Reconstruction recipe produced an unexpected length.");
  output->setpos(0);
}

} // namespace routed
