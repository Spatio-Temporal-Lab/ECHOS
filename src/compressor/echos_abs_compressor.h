#ifndef ECHOS_ABS_COMPRESSOR_H_
#define ECHOS_ABS_COMPRESSOR_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/echos_ablation_mode.h"
#include "utils/output_bit_stream.h"

class EchosAbsCompressor {
 public:
  EchosAbsCompressor(int block_size, double max_diff,
                     EchosAbsMode mode = EchosAbsMode::kAdaptive,
                     std::size_t sliding_window = kDefaultEchosSlidingWindow);

  void SetBlockConfig(int block_size, double max_diff);
  void AddValue(double value);
  void Close();

  Array<uint8_t> compressed_bytes() const;
  long get_compressed_size_in_bits() const;
  long get_decision_metadata_size_in_bits() const;

 private:
  void WriteMetadata();
  void UpdatePrediction(double recovered);
  void EncodeBatchOracle();

  int block_size_;
  double max_diff_;
  double quantization_step_;
  double inverse_quantization_step_;
  std::unique_ptr<EchosOutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_ = 2;
  AdaptiveQtCodec::AdaptiveDeltaRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int previous_block_size_ = 0;
  uint64_t previous_max_diff_bits_ = 0;
  int value_count_ = 0;
  long compressed_size_in_bits_ = 0;
  long stored_compressed_size_in_bits_ = 0;
  EchosAbsMode mode_;
  std::size_t sliding_window_;
  std::deque<std::array<uint64_t, 3>> context_window_;
  std::vector<double> pending_values_;
  long decision_metadata_size_in_bits_ = 0;
  long stored_decision_metadata_size_in_bits_ = 0;
};

#endif  // ECHOS_ABS_COMPRESSOR_H_
