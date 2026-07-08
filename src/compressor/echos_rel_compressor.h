#ifndef ECHOS_REL_COMPRESSOR_H_
#define ECHOS_REL_COMPRESSOR_H_

#include <cstdint>
#include <memory>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/output_bit_stream.h"

class EchosRelCompressor {
 public:
  EchosRelCompressor(int block_size, double relative_error_bound);

  void SetBlockConfig(int block_size, double relative_error_bound);
  void AddValue(double value);
  void Close();

  Array<uint8_t> compressed_bytes() const;
  long get_compressed_size_in_bits() const;

 private:
  enum class Mode {
    kRepeat,
    kSameSignResidual,
    kChangedSignResidual,
    kChangedSignZeroResidual,
    kZero,
    kRaw
  };

  struct Choice {
    Mode mode = Mode::kRaw;
    uint32_t rice_parameter = 0;
    uint64_t rice_bits = 0;
    uint64_t mapped = 0;
    double original_log = 0;
    double recovered_log = 0;
    double recovered_value = 0;
    bool sign = false;
    bool has_original_log = false;
    uint64_t bits = 0;
  };

  void UpdateErrorConfig(double relative_error_bound);
  Choice Choose(double value) const;
  void WriteMetadata();
  void WriteResidual(const Choice &choice);
  void WriteChoice(const Choice &choice, double original);
  void UpdateState(const Choice &choice, double original);

  int block_size_;
  double relative_error_bound_;
  double log_max_diff_;
  double inverse_log_step_;
  double lower_log_error_bound_;
  double upper_log_error_bound_;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  uint64_t adaptive_magnitude_sum_ = 0;
  uint64_t adaptive_sample_count_ = 0;
  bool metadata_initialized_ = false;
  int previous_block_size_ = 0;
  uint64_t previous_log_max_diff_bits_ = 0;
  int value_count_ = 0;
  long compressed_size_in_bits_ = 0;
  long stored_compressed_size_in_bits_ = 0;
};

#endif  // ECHOS_REL_COMPRESSOR_H_
