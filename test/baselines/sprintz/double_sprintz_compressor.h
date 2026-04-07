#ifndef DOUBLE_SPRINTZ_COMPRESSOR_H_
#define DOUBLE_SPRINTZ_COMPRESSOR_H_

#include <cstdint>
#include <cmath>
#include <vector>
#include <cstring>

#include "sprintz.h"

class DoubleSprintzCompressor {
 public:
  explicit DoubleSprintzCompressor(double max_diff);

  int compress(std::vector<double> data, int16_t *compression_output);

 private:
  const double kMaxDiff;
  double pre_value_ = 2;
};

#endif // DOUBLE_SPRINTZ_COMPRESSOR_H_
