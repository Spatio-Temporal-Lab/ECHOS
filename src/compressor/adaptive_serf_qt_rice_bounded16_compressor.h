#ifndef ADAPTIVE_SERF_QT_RICE_BOUNDED16_COMPRESSOR_H_
#define ADAPTIVE_SERF_QT_RICE_BOUNDED16_COMPRESSOR_H_

#include <cstdint>
#include <memory>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/output_bit_stream.h"

class AdaptiveSerfQtRiceBounded16Compressor {
 public:
  AdaptiveSerfQtRiceBounded16Compressor(int block_size, double max_diff);

  void SetBlockConfig(int block_size, double max_diff);
  void AddValue(double value);
  void Close();

  Array<uint8_t> compressed_bytes() const;
  long get_compressed_size_in_bits() const;

 private:
  void WriteMetadata();
  void UpdatePrediction(double recovered);

  int block_size_;
  double max_diff_;
  double quantization_step_;
  double inverse_quantization_step_;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_ = 2;
  AdaptiveQtCodec::AdaptiveBoundedRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int previous_block_size_ = 0;
  uint64_t previous_max_diff_bits_ = 0;
  int value_count_ = 0;
  long compressed_size_in_bits_ = 0;
  long stored_compressed_size_in_bits_ = 0;
};

#endif  // ADAPTIVE_SERF_QT_RICE_BOUNDED16_COMPRESSOR_H_
