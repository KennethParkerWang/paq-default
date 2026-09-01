#include "File.hpp"

void File::append(const char *s) {
  for( int i = 0; s[i] != 0; i++ ) {
    putChar(static_cast<uint8_t>(s[i]));
  }
}

uint32_t File::get32() {
  uint32_t value = 0;
  for (unsigned i = 0; i < 4; ++i) {
    const int c = getchar();
    if (c == EOF)
      quit("Corrupted stream: truncated 32-bit field.");
    value = (value << 8) | static_cast<uint8_t>(c);
  }
  return value;
}

void File::put32(uint32_t x) {
  putChar((x >> 24) & 255);
  putChar((x >> 16) & 255);
  putChar((x >> 8) & 255);
  putChar(x & 255);
}

uint64_t File::getVLI() {
  uint64_t value = 0;
  for (unsigned byteIndex = 0; byteIndex < 10; ++byteIndex) {
    const int c = getchar();
    if (c == EOF)
      quit("Corrupted stream: truncated variable-length integer.");
    const uint8_t byte = static_cast<uint8_t>(c);
    if (byteIndex == 9 && (byte & 0xFEu) != 0)
      quit("Corrupted stream: variable-length integer overflow.");
    value |= static_cast<uint64_t>(byte & 0x7Fu) << (byteIndex * 7);
    if ((byte & 0x80u) == 0) {
      if (byteIndex != 0 && (byte & 0x7Fu) == 0)
        quit("Corrupted stream: non-canonical variable-length integer.");
      return value;
    }
  }
  quit("Corrupted stream: unterminated variable-length integer.");
  return 0;
}

void File::putVLI(uint64_t i) {
  while( i > 0x7F ) {
    putChar(0x80 | (i & 0x7F));
    i >>= 7;
  }
  putChar(uint8_t(i));
}
