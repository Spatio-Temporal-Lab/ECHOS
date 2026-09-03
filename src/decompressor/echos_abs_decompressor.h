#ifndef ECHOS_ABS_DECOMPRESSOR_H_
#define ECHOS_ABS_DECOMPRESSOR_H_

#include <array>
#include <cstddef>
#include <deque>
#include <vector>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/echos_ablation_mode.h"

class EchosAbsDecompressor {
 public:
  explicit EchosAbsDecompressor(
      EchosAbsMode mode = EchosAbsMode::kAdaptive,
      std::size_t sliding_window = kDefaultEchosSlidingWindow);

  std::vector<double> Decompress(const Array<uint8_t> &bytes);

 private:
  EchosAbsMode mode_;
  std::size_t sliding_window_;
  std::deque<std::array<uint64_t, 3>> context_window_;
  double previous_ = 2;
  AdaptiveQtCodec::AdaptiveDeltaRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int block_size_ = 0;
  double max_diff_ = 0;
  double quantization_step_ = 0;
};

#endif  // ECHOS_ABS_DECOMPRESSOR_H_
