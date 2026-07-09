#ifndef ECHOS_ABS_DECOMPRESSOR_32_H_
#define ECHOS_ABS_DECOMPRESSOR_32_H_

#include <vector>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"

class EchosAbsDecompressor32 {
 public:
  std::vector<float> Decompress(const Array<uint8_t> &bytes);

 private:
  float previous_ = 2.0f;
  AdaptiveQtCodec::AdaptiveDeltaRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int block_size_ = 0;
  float max_diff_ = 0;
  float quantization_step_ = 0;
};

#endif  // ECHOS_ABS_DECOMPRESSOR_32_H_
