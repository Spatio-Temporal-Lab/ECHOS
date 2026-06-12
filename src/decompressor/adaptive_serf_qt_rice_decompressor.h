#ifndef ADAPTIVE_SERF_QT_RICE_DECOMPRESSOR_H_
#define ADAPTIVE_SERF_QT_RICE_DECOMPRESSOR_H_

#include <vector>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"

class AdaptiveSerfQtRiceDecompressor {
 public:
  std::vector<double> Decompress(const Array<uint8_t> &bytes);

 private:
  double previous_ = 2;
  AdaptiveQtCodec::AdaptiveRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int block_size_ = 0;
  double max_diff_ = 0;
};

#endif  // ADAPTIVE_SERF_QT_RICE_DECOMPRESSOR_H_
