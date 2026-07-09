#ifndef ECHOS_ABS_COMPRESSOR_32_H_
#define ECHOS_ABS_COMPRESSOR_32_H_

#include <cstdint>
#include <memory>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/output_bit_stream.h"

class EchosAbsCompressor32 {
 public:
  EchosAbsCompressor32(int block_size, float max_diff);

  void SetBlockConfig(int block_size, float max_diff);
  void AddValue(float value);
  void Close();

  Array<uint8_t> compressed_bytes() const;
  long get_compressed_size_in_bits() const;

 private:
  void WriteMetadata();
  void UpdatePrediction(float recovered);

  int block_size_;
  float max_diff_;
  float quantization_step_;
  float inverse_quantization_step_;
  std::unique_ptr<EchosOutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  float previous_ = 2.0f;
  AdaptiveQtCodec::AdaptiveDeltaRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int previous_block_size_ = 0;
  uint32_t previous_max_diff_bits_ = 0;
  int value_count_ = 0;
  long compressed_size_in_bits_ = 0;
  long stored_compressed_size_in_bits_ = 0;
};

#endif  // ECHOS_ABS_COMPRESSOR_32_H_
