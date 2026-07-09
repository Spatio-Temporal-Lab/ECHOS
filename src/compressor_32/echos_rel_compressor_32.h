#ifndef ECHOS_REL_COMPRESSOR_32_H_
#define ECHOS_REL_COMPRESSOR_32_H_

#include <cstdint>
#include <memory>

#include "utils/array.h"
#include "utils/output_bit_stream.h"

class EchosRelCompressor32 {
 public:
  EchosRelCompressor32(int block_size, float relative_error_bound);

  void SetBlockConfig(int block_size, float relative_error_bound);
  void AddValue(float value);
  void Close();

  Array<uint8_t> compressed_bytes() const;
  long get_compressed_size_in_bits() const;

 private:
  void UpdateErrorConfig(float relative_error_bound);
  void WriteMetadata();

  int block_size_;
  float relative_error_bound_;
  float log_max_diff_;
  double inverse_log_step_;
  double lower_log_error_bound_;
  double upper_log_error_bound_;
  std::unique_ptr<EchosOutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_log_ = 0;
  float previous_value_ = 1.0f;
  bool previous_sign_ = false;
  uint64_t adaptive_magnitude_sum_ = 0;
  uint64_t adaptive_sample_count_ = 0;
  bool metadata_initialized_ = false;
  int previous_block_size_ = 0;
  uint32_t previous_log_max_diff_bits_ = 0;
  int value_count_ = 0;
  long compressed_size_in_bits_ = 0;
  long stored_compressed_size_in_bits_ = 0;
};

#endif  // ECHOS_REL_COMPRESSOR_32_H_
