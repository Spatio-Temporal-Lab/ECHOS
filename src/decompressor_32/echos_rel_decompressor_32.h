#ifndef ECHOS_REL_DECOMPRESSOR_32_H_
#define ECHOS_REL_DECOMPRESSOR_32_H_

#include <cstdint>
#include <vector>

#include "utils/array.h"

class EchosRelDecompressor32 {
 public:
  std::vector<float> Decompress(const Array<uint8_t> &bytes);

 private:
  double previous_log_ = 0;
  float previous_value_ = 1.0f;
  bool previous_sign_ = false;
  uint64_t adaptive_magnitude_sum_ = 0;
  uint64_t adaptive_sample_count_ = 0;
  bool metadata_initialized_ = false;
  int block_size_ = 0;
  float log_max_diff_ = 0;
};

#endif  // ECHOS_REL_DECOMPRESSOR_32_H_
