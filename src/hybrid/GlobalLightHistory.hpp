#pragma once

#include "../Utils.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace routed {

constexpr size_t kDefaultGlobalLightBytes = 1u * 1024u * 1024u;
constexpr size_t kMaximumGlobalLightBytes = 16u * 1024u * 1024u;

// GLOBAL_LIGHT observes reconstructed original bytes only. It does not update
// PAQ's full model set and therefore cannot be confused with FULL_SHADOW.
class GlobalLightHistory {
public:
  explicit GlobalLightHistory(size_t capacity = kDefaultGlobalLightBytes)
    : bytes_(capacity) {
    if (capacity == 0 || capacity > kMaximumGlobalLightBytes)
      quit("GLOBAL_LIGHT history capacity is outside its resource limit.");
  }

  void reset() {
    position_ = 0;
    size_ = 0;
    totalBytes_ = 0;
  }

  void update(const uint8_t* data, size_t length) {
    if (length != 0 && data == nullptr)
      quit("GLOBAL_LIGHT received an unavailable byte range.");
    for (size_t index = 0; index < length; ++index) {
      if (totalBytes_ == UINT64_MAX)
        quit("GLOBAL_LIGHT byte counter overflow.");
      bytes_[position_] = data[index];
      position_ = (position_ + 1) % bytes_.size();
      if (size_ < bytes_.size())
        ++size_;
      ++totalBytes_;
    }
  }

  size_t size() const { return size_; }
  uint64_t totalBytes() const { return totalBytes_; }

  uint8_t byteFromDistance(size_t distance) const {
    if (distance == 0 || distance > size_)
      quit("GLOBAL_LIGHT history distance is unavailable.");
    const size_t index = (position_ + bytes_.size() - distance) % bytes_.size();
    return bytes_[index];
  }

  uint64_t contextHash(size_t length) const {
    if (length > size_)
      quit("GLOBAL_LIGHT context length is unavailable.");
    uint64_t hash = UINT64_C(1469598103934665603);
    for (size_t distance = length; distance != 0; --distance) {
      hash ^= byteFromDistance(distance);
      hash *= UINT64_C(1099511628211);
    }
    return hash;
  }

private:
  std::vector<uint8_t> bytes_;
  size_t position_ = 0;
  size_t size_ = 0;
  uint64_t totalBytes_ = 0;
};

} // namespace routed
