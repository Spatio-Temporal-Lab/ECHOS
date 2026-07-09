#ifndef ECHOS_FLOAT_H_
#define ECHOS_FLOAT_H_

#include <cstdint>
#include <cstring>
#include <limits>

class EchosFloat {
 public:
  static constexpr float kNan = std::numeric_limits<float>::quiet_NaN();

  static inline uint32_t FloatToIntBits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
  }

  static inline float IntBitsToFloat(uint32_t bits) {
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }
};

#endif  // ECHOS_FLOAT_H_
