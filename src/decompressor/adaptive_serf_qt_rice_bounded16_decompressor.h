#ifndef ADAPTIVE_SERF_QT_RICE_BOUNDED16_DECOMPRESSOR_H_
#define ADAPTIVE_SERF_QT_RICE_BOUNDED16_DECOMPRESSOR_H_

#include <vector>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"

class AdaptiveSerfQtRiceBounded16Decompressor {
 public:
  std::vector<double> Decompress(const Array<uint8_t> &bytes);

 private:
  double previous_ = 2;
  AdaptiveQtCodec::AdaptiveDeltaRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int block_size_ = 0;
  double max_diff_ = 0;
  double quantization_step_ = 0;
};

#endif  // ADAPTIVE_SERF_QT_RICE_BOUNDED16_DECOMPRESSOR_H_
