#ifndef DOUBLE_SPRINTZ_DECOMPRESSOR_H_
#define DOUBLE_SPRINTZ_DECOMPRESSOR_H_

#include <cstdint>
#include <vector>
#include <cstring>

#include "sprintz.h"

class DoubleSprintzDecompressor {
 public:
  DoubleSprintzDecompressor() = default;

  std::vector<double> decompress(int16_t *src);

 private:
  double max_diff_ = 0;
  double pre_value_ = 2;
};

#endif // DOUBLE_SPRINTZ_DECOMPRESSOR_H_
