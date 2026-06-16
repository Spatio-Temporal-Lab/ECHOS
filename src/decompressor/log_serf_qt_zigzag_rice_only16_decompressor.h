#ifndef LOG_SERF_QT_ZIGZAG_RICE_ONLY16_DECOMPRESSOR_H_
#define LOG_SERF_QT_ZIGZAG_RICE_ONLY16_DECOMPRESSOR_H_

#include <vector>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"

class LogSerfQtZigZagRiceOnly16Decompressor {
 public:
  std::vector<double> Decompress(const Array<uint8_t> &bytes);

 private:
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  AdaptiveQtCodec::AdaptiveRiceParameterState adaptive_state_{};
  bool metadata_initialized_ = false;
  int block_size_ = 0;
  double log_max_diff_ = 0;
};

#endif  // LOG_SERF_QT_ZIGZAG_RICE_ONLY16_DECOMPRESSOR_H_
