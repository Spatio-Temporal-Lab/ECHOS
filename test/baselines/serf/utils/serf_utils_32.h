#ifndef SERF_32_UTILS_H
#define SERF_32_UTILS_H

#include <cstdint>

#include "float.h"

class SerfUtils32 {
 public:
  static uint32_t FindAppInt(float min, float max, float v, uint32_t last_int, float max_diff);

 private:
  static constexpr uint32_t kBitWeight[32] = {
      1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 256U, 512U, 1024U,
      2048U, 4096U, 8192U, 16384U, 32768U, 65536U, 131072U, 262144U,
      524288U, 1048576U, 2097152U, 4194304U, 8388608U, 16777216U, 33554432U,
      67108864U, 134217728U, 268435456U, 536870912U, 1073741824U, 2147483648U
  };

  static uint32_t FindAppInt(float min_float, float max_float, uint32_t sign, float original, uint32_t last_int,
                             float max_diff);
};

#endif  // SERF_32_UTILS_H
