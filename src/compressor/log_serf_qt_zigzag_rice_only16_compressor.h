#ifndef LOG_SERF_QT_ZIGZAG_RICE_ONLY16_COMPRESSOR_H_
#define LOG_SERF_QT_ZIGZAG_RICE_ONLY16_COMPRESSOR_H_

#include <cstdint>
#include <memory>

#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/output_bit_stream.h"

class LogSerfQtZigZagRiceOnly16Compressor {
 public:
  LogSerfQtZigZagRiceOnly16Compressor(int block_size, double relative_error_bound);

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
    Mode mode;
    AdaptiveQtCodec::AdaptiveRiceChoice integer_choice;
    AdaptiveQtCodec::AdaptiveDeltaRiceCodeLengths lengths;
    uint64_t mapped;
    double original_log;
    double recovered_log;
    double recovered_value;
    bool sign;
    bool has_original_log;
    uint64_t bits;
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
  AdaptiveQtCodec::AdaptiveDeltaRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int previous_block_size_ = 0;
  uint64_t previous_log_max_diff_bits_ = 0;
  int value_count_ = 0;
  long compressed_size_in_bits_ = 0;
  long stored_compressed_size_in_bits_ = 0;
};

#endif  // LOG_SERF_QT_ZIGZAG_RICE_ONLY16_COMPRESSOR_H_
