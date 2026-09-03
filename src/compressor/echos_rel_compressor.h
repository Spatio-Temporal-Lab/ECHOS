#ifndef ECHOS_REL_COMPRESSOR_H_
#define ECHOS_REL_COMPRESSOR_H_

#include <cstdint>
#include <memory>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/output_bit_stream.h"

class EchosRelCompressor {
 public:
  EchosRelCompressor(int block_size, double relative_error_bound,
                     bool explicit_flags = false);

  void SetBlockConfig(int block_size, double relative_error_bound);
  void AddValue(double value);
  void Close();

  Array<uint8_t> compressed_bytes() const;
  long get_compressed_size_in_bits() const;

 private:
  void UpdateErrorConfig(double relative_error_bound);
  void WriteMetadata();
  void AddValueExplicitFlags(double value);

  int block_size_;
  double relative_error_bound_;
  double log_max_diff_;
  double inverse_log_step_;
  double lower_log_error_bound_;
  double upper_log_error_bound_;
  std::unique_ptr<EchosOutputBitStream> output_;
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
  bool explicit_flags_;
};

#endif  // ECHOS_REL_COMPRESSOR_H_
