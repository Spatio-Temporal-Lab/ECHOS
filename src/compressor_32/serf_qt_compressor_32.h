#ifndef SERF_QT_COMPRESSOR_32_H
#define SERF_QT_COMPRESSOR_32_H

#include <cstdint>
#include <memory>
#include <cmath>

#include "utils/output_bit_stream.h"
#include "utils/float.h"
#include "utils/elias_gamma_codec.h"
#include "utils/zig_zag_codec.h"

class SerfQtCompressor32 {
 public:
  explicit SerfQtCompressor32(int block_size, float max_diff);

  void AddValue(float v);

  Array<uint8_t> compressed_bytes();

  void Close();

  long stored_compressed_size_in_bits() const;

 private:
  const int kBlockSize;
  const float kMaxDiff;
  bool first_ = true;
  std::unique_ptr<OutputBitStream> output_bit_stream_;
  float pre_value_ = 2;
  Array<uint8_t> compressed_bytes_;
  long compressed_size_in_bits_ = 0;
  long stored_compressed_size_in_bits_ = 0;
};

#endif  // SERF_QT_COMPRESSOR_32_H
