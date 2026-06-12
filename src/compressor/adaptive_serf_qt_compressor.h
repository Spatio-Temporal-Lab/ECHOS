#ifndef ADAPTIVE_SERF_QT_COMPRESSOR_H_
#define ADAPTIVE_SERF_QT_COMPRESSOR_H_

#include <cstdint>
#include <memory>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/output_bit_stream.h"

class AdaptiveSerfQtCompressor {
 public:
  // AddValue makes an immediate, history-only coding decision and never buffers values.
  AdaptiveSerfQtCompressor(int block_size, double max_diff);

  void AddValue(double value);
  void Close();

  Array<uint8_t> compressed_bytes() const;
  long get_compressed_size_in_bits() const;

 private:
  AdaptiveQtCodec::IntegerCodec CurrentCodec() const;
  void UpdateState(double recovered, uint64_t mapped, bool raw);

  const int kBlockSize;
  const double kMaxDiff;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_ = 2;
  uint64_t gamma_cost_ = 0;
  uint64_t delta_cost_ = 0;
  int value_count_ = 0;
  long compressed_size_in_bits_ = 0;
  long stored_compressed_size_in_bits_ = 0;
};

#endif  // ADAPTIVE_SERF_QT_COMPRESSOR_H_
