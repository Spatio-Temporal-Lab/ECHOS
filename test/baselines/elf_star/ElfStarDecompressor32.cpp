#include <cstdint>
#include <cmath>
#include <sys/types.h>

#include "defs.h"
#include "BitStream/BitReader.h"
#include "utils/post_office_solver_32.h"
#include "utils/array.h"

class ElfStarXORDecompressor32 {
 private:
  FLOAT storedVal = {.i = 0};
  int storedLeadingZeros = __INT32_MAX__;
  int storedTrailingZeros = __INT32_MAX__;
  bool first = true;
  bool endOfStream = false;
  BitReader reader;

  Array<int> leadingRepresentation;
  Array<int> trailingRepresentation;
  int leadingBitsPerValue = 0;
  int trailingBitsPerValue = 0;

  int read_int(int length) { return readInt(&reader, length); }

  void initLeadingRepresentation() {
    int num = read_int(4);
    if (num == 0) {
      num = 16;
    }
    leadingBitsPerValue = PostOfficeSolver32::kPositionLength2Bits[num];
    leadingRepresentation = Array<int>(num);
    for (int i = 0; i < num; ++i) {
      leadingRepresentation[i] = read_int(5);
    }
  }

  void initTrailingRepresentation() {
    int num = read_int(4);
    if (num == 0) {
      num = 16;
    }
    trailingBitsPerValue = PostOfficeSolver32::kPositionLength2Bits[num];
    trailingRepresentation = Array<int>(num);
    for (int i = 0; i < num; ++i) {
      trailingRepresentation[i] = read_int(5);
    }
  }

  void next() {
    if (first) {
      initLeadingRepresentation();
      initTrailingRepresentation();
      first = false;
      int trailingZeros = read_int(6);
      if (trailingZeros < 32) {
        storedVal.i = (
            (static_cast<uint32_t>(read_int(31 - trailingZeros)) << 1) + 1) << trailingZeros;
      } else {
        storedVal.i = 0;
      }
      if (std::isnan(storedVal.f)) {
        endOfStream = true;
      }
    } else {
      nextValue();
    }
  }

  void nextValue() {
    FLOAT value;
    int centerBits;

    if (read_int(1) == 1) {
      centerBits = 32 - storedLeadingZeros - storedTrailingZeros;
      value.i = static_cast<uint32_t>(read_int(centerBits)) << storedTrailingZeros;
      value.i = storedVal.i ^ value.i;
      if (std::isnan(value.f)) {
        endOfStream = true;
      } else {
        storedVal = value;
      }
    } else if (read_int(1) == 0) {
      int leadAndTrail = read_int(leadingBitsPerValue + trailingBitsPerValue);
      int lead = leadAndTrail >> trailingBitsPerValue;
      int trailMask = (trailingBitsPerValue == 0) ? 0 : ((1 << trailingBitsPerValue) - 1);
      int trail = leadAndTrail & trailMask;
      storedLeadingZeros = leadingRepresentation[lead];
      storedTrailingZeros = trailingRepresentation[trail];
      centerBits = 32 - storedLeadingZeros - storedTrailingZeros;

      value.i = static_cast<uint32_t>(read_int(centerBits)) << storedTrailingZeros;
      value.i = storedVal.i ^ value.i;
      if (std::isnan(value.f)) {
        endOfStream = true;
      } else {
        storedVal = value;
      }
    }
  }

 public:
  size_t length = 0;

  void init(uint32_t *in, size_t len) {
    initBitReader(&reader, in + 1, len - 1);
    length = in[0];
  }

  float readValue() {
    next();
    if (endOfStream) {
      return -1.0f;
    }
    return storedVal.f;
  }

  BitReader *getReader() {
    return &reader;
  }

  void refresh() {
    storedVal = {.i = 0};
    storedLeadingZeros = __INT32_MAX__;
    storedTrailingZeros = __INT32_MAX__;
    first = true;
    endOfStream = false;
    leadingBitsPerValue = 0;
    trailingBitsPerValue = 0;
  }
};

class ElfStarDecompressor32 {
 private:
  ElfStarXORDecompressor32 xorDecompressor;
  int lastBetaStar = __INT32_MAX__;

  float nextValue() {
    float v;
    if (read_int(1) == 0) {
      v = recoverVByBetaStar();
    } else if (read_int(1) == 0) {
      v = xorDecompressor.readValue();
    } else {
      lastBetaStar = read_int(3);
      v = recoverVByBetaStar();
    }
    return v;
  }

  float recoverVByBetaStar() {
    float v;
    float vPrime = xorDecompressor.readValue();
    int sp = elfstar_utils::getSP(std::fabs(vPrime));
    if (lastBetaStar == 0) {
      v = elfstar_utils::get10iN_32(-sp - 1);
      if (vPrime < 0) {
        v = -v;
      }
    } else {
      int alpha = lastBetaStar - sp - 1;
      v = elfstar_utils::roundUp_32(vPrime, alpha);
    }
    return v;
  }

 protected:
  int read_int(int len) {
    int res = readInt(xorDecompressor.getReader(), len);
    return res;
  }

  int getLength() {
    return xorDecompressor.length;
  }

 public:
  void init(uint32_t *in, size_t len) {
    xorDecompressor.init(in, len);
  }

  int decompress(float *output) {
    int len = getLength();
    for (int i = 0; i < len; ++i) {
      output[i] = nextValue();
    }
    return len;
  }

  void refresh() {
    lastBetaStar = __INT32_MAX__;
    xorDecompressor.refresh();
  }
};

extern "C" ssize_t elf_star_decode_32(uint8_t *in, ssize_t len, float *out) {
  ElfStarDecompressor32 decompressor;
  decompressor.init(reinterpret_cast<uint32_t *>(in), len / 4);
  return decompressor.decompress(out);
}
