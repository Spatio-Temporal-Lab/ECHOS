#include <cstdint>
#include <cmath>
#include <sys/types.h>
#include <bits/stdc++.h>

#include "elf_star.h"
#include "defs.h"
#include "BitStream/BitWriter.h"
#include "utils/post_office_solver_32.h"
#include "utils/output_bit_stream.h"
#include "utils/array.h"

using namespace std;

class ElfStarXORCompressor32 {
 private:
  Array<int> leading_representation_ = Array<int>(32);
  Array<int> trailing_representation_ = Array<int>(32);
  Array<int> leading_round_ = Array<int>(32);
  Array<int> trailing_round_ = Array<int>(32);
  int storedLeadingZeros = __INT32_MAX__;
  int storedTrailingZeros = __INT32_MAX__;
  uint32_t storedVal = 0;
  bool first = true;
  Array<int> lead_distribution_ = Array<int>(32);
  Array<int> trail_distribution_ = Array<int>(32);
  int leading_bits_per_value_ = 0;
  int trailing_bits_per_value_ = 0;

  size_t length = 0;
  BitWriter writer;
  uint32_t *output = nullptr;

  int initLeadingRoundAndRepresentation(Array<int> distribution) {
    Array<int> lead_positions = PostOfficeSolver32::InitRoundAndRepresentation(
        distribution, leading_representation_, leading_round_);
    leading_bits_per_value_ = PostOfficeSolver32::kPositionLength2Bits[lead_positions.length()];
    return PostOfficeSolver32::WritePositions(lead_positions, &writer);
  }

  int initTrailingRoundAndRepresentation(Array<int> distribution) {
    Array<int> trail_positions = PostOfficeSolver32::InitRoundAndRepresentation(
        distribution, trailing_representation_, trailing_round_);
    trailing_bits_per_value_ = PostOfficeSolver32::kPositionLength2Bits[trail_positions.length()];
    return PostOfficeSolver32::WritePositions(trail_positions, &writer);
  }

  int writeFirst(uint32_t value) {
    first = false;
    storedVal = value;
    int trailingZeros;
    if (value == 0) {
      trailingZeros = 32;
    } else {
      trailingZeros = __builtin_ctz(value);
    }
    write(&writer, trailingZeros, 6);
    if (value != 0) {
      writeLong(&writer, static_cast<uint64_t>(storedVal) >> (trailingZeros + 1), 31 - trailingZeros);
      return 37 - trailingZeros;
    } else {
      return 6;
    }
  }

  int compressValue(uint32_t value) {
    int thisSize = 0;
    uint32_t xor_value = storedVal ^ value;
    if (xor_value == 0) {
      write(&writer, 1, 2);
      thisSize += 2;
    } else {
      int leading_count = __builtin_clz(static_cast<unsigned int>(xor_value));
      int trailing_count = __builtin_ctz(static_cast<unsigned int>(xor_value));
      int leadingZeros = leading_round_[leading_count];
      int trailingZeros = trailing_round_[trailing_count];

      if (leadingZeros >= storedLeadingZeros &&
          trailingZeros >= storedTrailingZeros &&
          (leadingZeros - storedLeadingZeros) + (trailingZeros - storedTrailingZeros)
              < 1 + leading_bits_per_value_ + trailing_bits_per_value_) {
        int centerBits = 32 - storedLeadingZeros - storedTrailingZeros;
        int len = 1 + centerBits;
        if (len > 32) {
          write(&writer, 1, 1);
          writeLong(&writer, static_cast<uint64_t>(xor_value) >> storedTrailingZeros, centerBits);
        } else {
          writeLong(&writer,
                    (static_cast<uint64_t>(1) << centerBits) |
                        (static_cast<uint64_t>(xor_value) >> storedTrailingZeros),
                    len);
        }
        thisSize += len;
      } else {
        storedLeadingZeros = leadingZeros;
        storedTrailingZeros = trailingZeros;
        int centerBits = 32 - storedLeadingZeros - storedTrailingZeros;
        int len = 2 + leading_bits_per_value_ + trailing_bits_per_value_ + centerBits;
        uint64_t shifted = static_cast<uint64_t>(xor_value) >> storedTrailingZeros;
        if (len > 32) {
          uint32_t header = (static_cast<uint32_t>(leading_representation_[storedLeadingZeros])
                             << trailing_bits_per_value_) |
                            static_cast<uint32_t>(trailing_representation_[storedTrailingZeros]);
          write(&writer, header, 2 + leading_bits_per_value_ + trailing_bits_per_value_);
          writeLong(&writer, shifted, centerBits);
        } else {
          uint64_t header = (static_cast<uint64_t>(leading_representation_[storedLeadingZeros])
                             << trailing_bits_per_value_) |
                            static_cast<uint64_t>(trailing_representation_[storedTrailingZeros]);
          writeLong(&writer, (header << centerBits) | shifted, len);
        }
        thisSize += len;
      }
      storedVal = value;
    }
    return thisSize;
  }

 public:
  BitWriter *getWriter() {
    return &writer;
  }

  void init(size_t len) {
    len *= 12;
    output = static_cast<uint32_t *>(malloc(len + 4));
    initBitWriter(&writer, output + 1, len / sizeof(uint32_t));
  }

  int addValue(uint32_t value) {
    if (first) {
      return initLeadingRoundAndRepresentation(lead_distribution_) +
             initTrailingRoundAndRepresentation(trail_distribution_) +
             writeFirst(value);
    }
    return compressValue(value);
  }

  void close() {
    flush(&writer);
  }

  uint32_t *getOut() {
    return output;
  }

  void setDistribution(Array<int> lead_distribution, Array<int> trail_distribution) {
    lead_distribution_ = lead_distribution;
    trail_distribution_ = trail_distribution;
  }

  void refresh() {
    output = static_cast<uint32_t *>(malloc(length + 4));
    storedLeadingZeros = __INT32_MAX__;
    storedTrailingZeros = __INT32_MAX__;
    storedVal = 0;
    first = true;
    for (int i = 0; i < 32; ++i) {
      leading_representation_[i] = 0;
      trailing_representation_[i] = 0;
      leading_round_[i] = 0;
      trailing_round_[i] = 0;
    }
  }
};

class ElfStarCompressor32 {
 private:
  size_t size = 32;
  int lastBetaStar = __INT32_MAX__;
  int numberOfValues = 0;
  ElfStarXORCompressor32 xorCompressor;
  int *betaStarList = nullptr;
  uint32_t *vPrimeList = nullptr;
  Array<int> leadDistribution = Array<int>(32);
  Array<int> trailDistribution = Array<int>(32);

 protected:
  int writeInt(int n, int len) {
    write(xorCompressor.getWriter(), n, len);
    return len;
  }

  int writeBit(bool bit) {
    write(xorCompressor.getWriter(), bit, 1);
    return 1;
  }

  int xorCompress(uint32_t vPrimeInt) {
    return xorCompressor.addValue(vPrimeInt);
  }

 public:
  void addValue(float v) {
    FLOAT data = {.f = v};
    uint32_t vPrimeInt;
    if (v == 0.0f || std::isinf(v)) {
      vPrimeList[numberOfValues] = data.i;
      betaStarList[numberOfValues] = __INT32_MAX__;
    } else if (std::isnan(v)) {
      vPrimeList[numberOfValues] = 0x7fc00000;
      betaStarList[numberOfValues] = __INT32_MAX__;
    } else {
      int *alphaAndBetaStar = elfstar_utils::getAlphaAndBetaStar_32(v, lastBetaStar);
      int e = (data.i >> 23) & 0xff;
      int gAlpha = elfstar_utils::getFAlpha(alphaAndBetaStar[0]) + e - 127;
      int eraseBits = 23 - gAlpha;
      uint32_t mask = 0xffffffffu << eraseBits;
      uint32_t delta = (~mask) & data.i;
      if (delta != 0 && eraseBits > 3) {
        if (alphaAndBetaStar[1] != lastBetaStar) {
          lastBetaStar = alphaAndBetaStar[1];
        }
        betaStarList[numberOfValues] = lastBetaStar;
        vPrimeList[numberOfValues] = mask & data.i;
      } else {
        betaStarList[numberOfValues] = __INT32_MAX__;
        vPrimeList[numberOfValues] = data.i;
      }
      delete[] alphaAndBetaStar;
    }
    numberOfValues++;
  }

  void calculateDistribution() {
    uint32_t lastValue = vPrimeList[0];
    for (int i = 1; i < numberOfValues; ++i) {
      uint32_t xor_value = lastValue ^ vPrimeList[i];
      if (xor_value != 0) {
        leadDistribution[__builtin_clz(static_cast<unsigned int>(xor_value))]++;
        trailDistribution[__builtin_ctz(static_cast<unsigned int>(xor_value))]++;
        lastValue = vPrimeList[i];
      }
    }
  }

  void compress() {
    xorCompressor.setDistribution(leadDistribution, trailDistribution);
    lastBetaStar = __INT32_MAX__;
    for (int i = 0; i < numberOfValues; ++i) {
      if (betaStarList[i] == __INT32_MAX__) {
        size += writeInt(2, 2);
      } else if (betaStarList[i] == lastBetaStar) {
        size += writeBit(false);
      } else {
        size += writeInt(betaStarList[i] | 0x18, 5);
        lastBetaStar = betaStarList[i];
      }
      size += xorCompressor.addValue(vPrimeList[i]);
    }
  }

  int getSize() const {
    return size;
  }

  void init(int length) {
    xorCompressor.init(length);
    betaStarList = new int[length];
    vPrimeList = new uint32_t[length];
  }

  uint32_t *getBytes() {
    return xorCompressor.getOut();
  }

  void close() {
    calculateDistribution();
    compress();
    xorCompressor.close();
    *getBytes() = numberOfValues;
    delete[] betaStarList;
    delete[] vPrimeList;
  }

  void refresh() {
    xorCompressor.refresh();
    size = 0;
    lastBetaStar = __INT32_MAX__;
    numberOfValues = 0;
    leadDistribution = Array<int>(32);
    trailDistribution = Array<int>(32);
  }
};

ssize_t elf_star_encode_32(float *in, ssize_t len, uint8_t **out) {
  ElfStarCompressor32 compressor;
  compressor.init(len);
  for (int i = 0; i < len; ++i) {
    compressor.addValue(in[i]);
  }
  compressor.close();
  *out = reinterpret_cast<uint8_t *>(compressor.getBytes());
  return (compressor.getSize() + 31) / 32 * 4;
}
