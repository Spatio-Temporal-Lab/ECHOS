#ifndef ECHOS_REL_DECOMPRESSOR_H_
#define ECHOS_REL_DECOMPRESSOR_H_

#include <vector>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"

class EchosRelDecompressor {
 public:
  std::vector<double> Decompress(const Array<uint8_t> &bytes);

 private:
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  uint64_t adaptive_magnitude_sum_ = 0;
  uint64_t adaptive_sample_count_ = 0;
  bool metadata_initialized_ = false;
  int block_size_ = 0;
  double log_max_diff_ = 0;
};

#endif  // ECHOS_REL_DECOMPRESSOR_H_
