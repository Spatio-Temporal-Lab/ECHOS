#ifndef ADAPTIVE_SERF_QT_DECOMPRESSOR_H_
#define ADAPTIVE_SERF_QT_DECOMPRESSOR_H_

#include <cstdint>
#include <vector>

#include "utils/array.h"

class AdaptiveSerfQtDecompressor {
 public:
  std::vector<double> Decompress(const Array<uint8_t> &bytes);

 private:
  double previous_ = 2;
  uint64_t gamma_cost_ = 0;
  uint64_t delta_cost_ = 0;
};

#endif  // ADAPTIVE_SERF_QT_DECOMPRESSOR_H_
