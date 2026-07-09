#include "utils/serf_utils_32.h"

uint32_t SerfUtils32::FindAppInt(float min, float max, float v, uint32_t last_int, float max_diff) {
  if (min >= 0) {
    // both positive
    return FindAppInt(min, max, 0, v, last_int, max_diff);
  } else if (max <= 0) {
    // both negative
    return FindAppInt(-max, -min, 0x80000000, v, last_int, max_diff);
  } else if (last_int >> 31 == 0) {
    // consider positive part only, to make more leading zeros
    return FindAppInt(0, max, 0, v, last_int, max_diff);
  } else {
    // consider negative part only, to make more leading zeros
    return FindAppInt(0, -min, 0x80000000, v, last_int, max_diff);
  }
}

uint32_t
SerfUtils32::FindAppInt(float min_float, float max_float, uint32_t sign, float original, uint32_t last_int,
                        float max_diff) {
  // may be negative zero
  uint32_t min = Float::FloatToIntBits(min_float) & 0x7fffffff;
  uint32_t max = Float::FloatToIntBits(max_float);
  int leading_zeros = __builtin_clz(min ^ max);
  int32_t front_mask = 0xffffffff << (32 - leading_zeros);
  int shift = 32 - leading_zeros;
  uint32_t result_int;
  float diff;
  uint32_t append;
  while (shift >= 0) {
    uint32_t front = front_mask & min;
    uint32_t rear = (~front_mask) & last_int;

    append = rear | front;
    if (append >= min && append <= max) {
      result_int = append ^ sign;
      diff = Float::IntBitsToFloat(result_int) - original;
      if (diff >= -max_diff && diff <= max_diff) {
        return result_int;
      }
    }

    append = (append + kBitWeight[shift]) & 0x7fffffff;  // may be overflow
    if (append <= max) {
      // append must be greater than min
      result_int = append ^ sign;
      diff = Float::IntBitsToFloat(result_int) - original;
      if (diff >= -max_diff && diff <= max_diff) {
        return result_int;
      }
    }

    front_mask = front_mask >> 1;

    --shift;
  }

  // we do not find a satisfied value, so we return the original value
  return Float::FloatToIntBits(original);
}
