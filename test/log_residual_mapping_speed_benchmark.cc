#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "Perf_expr_config.hpp"
#include "compressor/log_serf_qt_compressor.h"
#include "decompressor/log_serf_qt_decompressor.h"
#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/double.h"
#include "utils/input_bit_stream.h"
#include "utils/output_bit_stream.h"

namespace {

constexpr uint32_t kRiceQuotientCap = 16;
constexpr int kWarmups = 3;
constexpr int kRepetitions = 9;

#if defined(__GNUC__) || defined(__clang__)
#define SERF_NOINLINE __attribute__((noinline))
#else
#define SERF_NOINLINE
#endif

enum class ZigZagMode {
  kRepeat,
  kSameSignResidual,
  kChangedSignResidual,
  kChangedSignZeroResidual,
  kZero,
  kRaw
};

struct BenchmarkResult {
  uint64_t values = 0;
  uint64_t bits = 0;
  double compression_ms = 0;
  double decompression_ms = 0;
  bool valid = true;
};

bool QuantizeLog(double magnitude, double prediction, double log_max_diff, double inverse_log_step,
                 double lower_log_error_bound, double upper_log_error_bound, int64_t *q,
                 double *target_log, double *recovered_log) {
  if (!std::isfinite(magnitude) || magnitude == 0) return false;
  *target_log = std::log(magnitude);
  const double scaled = (*target_log - prediction) * inverse_log_step;
  if (!std::isfinite(scaled)) return false;

  if (std::abs(scaled) > 0x1p52) {
    const long double step = 2.0L * static_cast<long double>(log_max_diff);
    const long double precise_scaled =
        (static_cast<long double>(*target_log) - static_cast<long double>(prediction)) / step;
    if (!std::isfinite(precise_scaled)) return false;
    const long double precise_rounded = std::round(precise_scaled);
    if (precise_rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
        precise_rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    *q = static_cast<int64_t>(precise_rounded);
  } else {
    *q = static_cast<int64_t>(std::round(scaled));
  }
  if (*q == std::numeric_limits<int64_t>::min()) return false;

  *recovered_log = prediction + 2 * log_max_diff * static_cast<double>(*q);
  const double log_error = *recovered_log - *target_log;
  return log_error >= lower_log_error_bound && log_error <= upper_log_error_bound;
}

uint64_t SelectedLength(const AdaptiveQtCodec::AdaptiveCodeLengths &lengths,
                        AdaptiveQtCodec::IntegerCodec codec) {
  switch (codec) {
    case AdaptiveQtCodec::IntegerCodec::kGamma:
      return lengths.gamma;
    case AdaptiveQtCodec::IntegerCodec::kDelta:
      return lengths.delta;
    case AdaptiveQtCodec::IntegerCodec::kRice:
      return lengths.rice;
    case AdaptiveQtCodec::IntegerCodec::kRaw:
      return 64;
  }
  throw std::runtime_error("Unknown integer codec");
}

ZigZagMode ReadMode(InputBitStream *input) {
  if (!input->ReadBit()) return ZigZagMode::kRepeat;
  if (!input->ReadBit()) return ZigZagMode::kSameSignResidual;
  if (!input->ReadBit()) return ZigZagMode::kChangedSignResidual;
  if (!input->ReadBit()) return ZigZagMode::kChangedSignZeroResidual;
  return !input->ReadBit() ? ZigZagMode::kZero : ZigZagMode::kRaw;
}

class ZigZagLogCompressor {
 public:
  ZigZagLogCompressor(int block_size, double relative_error_bound) : block_size_(block_size) {
    if (block_size <= 0 || block_size > 65535) throw std::invalid_argument("Invalid block size");
    if (!std::isfinite(relative_error_bound) || relative_error_bound <= 0) {
      throw std::invalid_argument("Invalid relative error bound");
    }
    relative_error_bound_ = relative_error_bound;
    upper_log_error_bound_ = std::log1p(relative_error_bound);
    lower_log_error_bound_ =
        relative_error_bound < 1 ? std::log1p(-relative_error_bound)
                                 : -std::numeric_limits<double>::infinity();
    log_max_diff_ = upper_log_error_bound_ * 0.999;
    inverse_log_step_ = 1.0 / (2 * log_max_diff_);
    output_ = std::make_unique<OutputBitStream>(static_cast<uint32_t>(16 * block_size + 32));
  }

  SERF_NOINLINE void AddValue(double value) {
    if (value_count_ >= block_size_) throw std::runtime_error("ZigZag Log Serf-QT block is full");
    if (value_count_ == 0) WriteMetadata();
    const Choice choice = Choose(value);
    WriteChoice(choice, value);
    UpdateState(choice, value);
    ++value_count_;
  }

  SERF_NOINLINE void Close() {
    if (value_count_ != block_size_) {
      throw std::runtime_error("ZigZag Log Serf-QT block is incomplete");
    }
    output_->Flush();
    compressed_bytes_ =
        output_->GetBuffer(static_cast<uint32_t>(std::ceil(compressed_size_in_bits_ / 8.0)));
    output_->Refresh();
    stored_compressed_size_in_bits_ = compressed_size_in_bits_;
    compressed_size_in_bits_ = 0;
    value_count_ = 0;
  }

  SERF_NOINLINE Array<uint8_t> compressed_bytes() const { return compressed_bytes_; }
  SERF_NOINLINE long get_compressed_size_in_bits() const { return stored_compressed_size_in_bits_; }

 private:
  struct Choice {
    ZigZagMode mode;
    AdaptiveQtCodec::AdaptiveRiceChoice integer_choice;
    AdaptiveQtCodec::AdaptiveCodeLengths lengths;
    uint64_t mapped;
    double original_log;
    double recovered_log;
    double recovered_value;
    bool sign;
    bool has_original_log;
    uint64_t bits;
  };

  void WriteMetadata() {
    const uint64_t log_max_diff_bits = Double::DoubleToLongBits(log_max_diff_);
    const bool block_size_changed = !metadata_initialized_ || block_size_ != previous_block_size_;
    const bool log_max_diff_changed =
        !metadata_initialized_ || log_max_diff_bits != previous_log_max_diff_bits_;
    compressed_size_in_bits_ += output_->WriteBit(block_size_changed);
    compressed_size_in_bits_ += output_->WriteBit(log_max_diff_changed);
    if (block_size_changed) compressed_size_in_bits_ += output_->WriteInt(block_size_, 16);
    if (log_max_diff_changed) compressed_size_in_bits_ += output_->WriteLong(log_max_diff_bits, 64);
    previous_block_size_ = block_size_;
    previous_log_max_diff_bits_ = log_max_diff_bits;
    metadata_initialized_ = true;
  }

  Choice Choose(double value) const {
    Choice best;
    best.mode = ZigZagMode::kRaw;
    best.sign = std::signbit(value);
    best.has_original_log = false;
    best.bits = 69;

    if (value == 0) {
      best.mode = ZigZagMode::kZero;
      best.bits = 5;
      return best;
    }

    const double magnitude = std::abs(value);
    if (std::isfinite(value) && best.sign == previous_sign_ &&
        std::abs(previous_value_ - value) <= relative_error_bound_ * magnitude) {
      best.mode = ZigZagMode::kRepeat;
      best.bits = 1;
      return best;
    }
    if (std::isfinite(value) && best.sign != previous_sign_ &&
        std::abs(std::abs(previous_value_) - magnitude) <= relative_error_bound_ * magnitude) {
      best.mode = ZigZagMode::kChangedSignZeroResidual;
      best.recovered_log = previous_log_;
      best.recovered_value = best.sign ? -std::abs(previous_value_) : std::abs(previous_value_);
      best.bits = 4;
      return best;
    }

    best.integer_choice = AdaptiveQtCodec::SelectAdaptiveRiceCodec(adaptive_state_);
    int64_t q = 0;
    double target_log = 0;
    double recovered_log = 0;
    if (!QuantizeLog(magnitude, previous_log_, log_max_diff_, inverse_log_step_,
                     lower_log_error_bound_, upper_log_error_bound_, &q, &target_log,
                     &recovered_log)) {
      return best;
    }
    best.original_log = target_log;
    best.has_original_log = true;
    if (q == 0) return best;

    const bool changed_sign = best.sign != previous_sign_;
    best.mapped = AdaptiveQtCodec::ZigZagEncode(q);
    best.recovered_log = recovered_log;
    best.lengths = AdaptiveQtCodec::CalculateCappedRiceCodeLengths(
        best.mapped, best.integer_choice.rice_parameter, kRiceQuotientCap);
    const uint64_t prefix_bits = changed_sign ? 3 : 2;
    const uint64_t residual_bits = SelectedLength(best.lengths, best.integer_choice.codec);
    if (prefix_bits + residual_bits < best.bits) {
      const double recovered_magnitude = std::exp(recovered_log);
      if (!std::isfinite(recovered_magnitude)) return best;
      best.mode =
          changed_sign ? ZigZagMode::kChangedSignResidual : ZigZagMode::kSameSignResidual;
      best.recovered_value = best.sign ? -recovered_magnitude : recovered_magnitude;
      best.bits = prefix_bits + residual_bits;
    }
    return best;
  }

  void WriteResidual(const Choice &choice) {
    if (choice.integer_choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
      compressed_size_in_bits_ += AdaptiveQtCodec::EncodeCappedRice(
          choice.mapped, choice.integer_choice.rice_parameter, kRiceQuotientCap, output_.get());
      return;
    }
    compressed_size_in_bits_ +=
        AdaptiveQtCodec::EncodeMapped(choice.mapped, choice.integer_choice.codec,
                                      choice.integer_choice.rice_parameter, output_.get());
  }

  void WriteChoice(const Choice &choice, double original) {
    switch (choice.mode) {
      case ZigZagMode::kRepeat:
        compressed_size_in_bits_ += output_->WriteBit(false);
        return;
      case ZigZagMode::kSameSignResidual:
        compressed_size_in_bits_ += output_->WriteInt(2, 2);
        WriteResidual(choice);
        return;
      case ZigZagMode::kChangedSignResidual:
        compressed_size_in_bits_ += output_->WriteInt(6, 3);
        WriteResidual(choice);
        return;
      case ZigZagMode::kChangedSignZeroResidual:
        compressed_size_in_bits_ += output_->WriteInt(14, 4);
        return;
      case ZigZagMode::kZero:
        compressed_size_in_bits_ += output_->WriteInt(30, 5);
        return;
      case ZigZagMode::kRaw:
        compressed_size_in_bits_ += output_->WriteInt(31, 5);
        compressed_size_in_bits_ += output_->WriteLong(Double::DoubleToLongBits(original), 64);
        return;
    }
  }

  void UpdateState(const Choice &choice, double original) {
    if (choice.mode == ZigZagMode::kRaw) {
      if (std::isfinite(original) && original != 0) {
        previous_log_ = choice.has_original_log ? choice.original_log : std::log(std::abs(original));
        previous_value_ = original;
        previous_sign_ = std::signbit(original);
      }
      return;
    }
    if (choice.mode == ZigZagMode::kZero || choice.mode == ZigZagMode::kRepeat) return;
    previous_log_ = choice.recovered_log;
    previous_value_ = choice.recovered_value;
    previous_sign_ = choice.sign;
    if (choice.mode != ZigZagMode::kChangedSignZeroResidual) {
      AdaptiveQtCodec::UpdateAdaptiveRiceState(choice.mapped, choice.lengths, &adaptive_state_);
    }
  }

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
  AdaptiveQtCodec::AdaptiveRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int previous_block_size_ = 0;
  uint64_t previous_log_max_diff_bits_ = 0;
  int value_count_ = 0;
  long compressed_size_in_bits_ = 0;
  long stored_compressed_size_in_bits_ = 0;
};

class ZigZagLogDecompressor {
 public:
  SERF_NOINLINE std::vector<double> Decompress(const Array<uint8_t> &bytes) {
    InputBitStream input;
    input.SetBuffer(bytes);
    const bool block_size_changed = input.ReadBit();
    const bool log_max_diff_changed = input.ReadBit();
    if (!metadata_initialized_ && (!block_size_changed || !log_max_diff_changed)) {
      throw std::runtime_error("First ZigZag Log Serf-QT block must contain full metadata");
    }
    if (block_size_changed) block_size_ = input.ReadInt(16);
    if (log_max_diff_changed) log_max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
    if (block_size_ <= 0 || block_size_ > 65535 || !std::isfinite(log_max_diff_) ||
        log_max_diff_ <= 0) {
      throw std::runtime_error("Invalid ZigZag Log Serf-QT metadata");
    }
    metadata_initialized_ = true;

    std::vector<double> result;
    result.reserve(block_size_);
    for (int index = 0; index < block_size_; ++index) {
      const ZigZagMode mode = ReadMode(&input);
      if (mode == ZigZagMode::kRaw) {
        const double value = Double::LongBitsToDouble(input.ReadLong(64));
        result.push_back(value);
        if (std::isfinite(value) && value != 0) {
          previous_log_ = std::log(std::abs(value));
          previous_value_ = value;
          previous_sign_ = std::signbit(value);
        }
        continue;
      }
      if (mode == ZigZagMode::kZero) {
        result.push_back(0.0);
        continue;
      }
      if (mode == ZigZagMode::kRepeat) {
        result.push_back(previous_value_);
        continue;
      }
      if (mode == ZigZagMode::kChangedSignZeroResidual) {
        previous_sign_ = !previous_sign_;
        previous_value_ = previous_sign_ ? -std::abs(previous_value_) : std::abs(previous_value_);
        result.push_back(previous_value_);
        continue;
      }

      const AdaptiveQtCodec::AdaptiveRiceChoice choice =
          AdaptiveQtCodec::SelectAdaptiveRiceCodec(adaptive_state_);
      uint64_t mapped;
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        mapped = AdaptiveQtCodec::DecodeCappedRice(choice.rice_parameter, kRiceQuotientCap, &input);
      } else {
        mapped = AdaptiveQtCodec::DecodeMapped(choice.codec, choice.rice_parameter, &input);
      }
      if (mode == ZigZagMode::kChangedSignResidual) previous_sign_ = !previous_sign_;
      const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped);
      previous_log_ += 2 * log_max_diff_ * static_cast<double>(q);
      const double magnitude = std::exp(previous_log_);
      previous_value_ = previous_sign_ ? -magnitude : magnitude;
      result.push_back(previous_value_);
      const AdaptiveQtCodec::AdaptiveCodeLengths lengths =
          AdaptiveQtCodec::CalculateCappedRiceCodeLengths(mapped, choice.rice_parameter,
                                                          kRiceQuotientCap);
      AdaptiveQtCodec::UpdateAdaptiveRiceState(mapped, lengths, &adaptive_state_);
    }
    return result;
  }

 private:
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  AdaptiveQtCodec::AdaptiveRiceState adaptive_state_{};
  bool metadata_initialized_ = false;
  int block_size_ = 0;
  double log_max_diff_ = 0;
};

std::vector<double> ReadValues(const std::filesystem::path &path) {
  std::ifstream input(path);
  std::vector<double> values;
  double value;
  while (input >> value) values.push_back(value);
  return values;
}

template <typename Compressor>
uint64_t CompressDatasets(const std::vector<std::vector<double>> &datasets, double relative_error,
                          std::vector<std::vector<Array<uint8_t>>> *blocks) {
  uint64_t bits = 0;
  if (blocks != nullptr) blocks->clear();
  for (const std::vector<double> &values : datasets) {
    Compressor compressor(kBlockSizeOverall, relative_error);
    if (blocks != nullptr) blocks->emplace_back();
    for (size_t begin = 0; begin + kBlockSizeOverall <= values.size(); begin += kBlockSizeOverall) {
      for (size_t index = begin; index < begin + kBlockSizeOverall; ++index) {
        compressor.AddValue(values[index]);
      }
      compressor.Close();
      bits += compressor.get_compressed_size_in_bits();
      if (blocks != nullptr) blocks->back().push_back(compressor.compressed_bytes());
    }
  }
  return bits;
}

bool WithinRelativeError(double original, double recovered, double relative_error) {
  if (std::isnan(original)) return std::isnan(recovered);
  if (std::isinf(original)) return original == recovered;
  if (original == 0) return recovered == 0;
  return std::abs(original - recovered) / std::abs(original) <= relative_error;
}

template <typename Decompressor>
bool DecompressDatasets(const std::vector<std::vector<double>> &datasets,
                        const std::vector<std::vector<Array<uint8_t>>> &blocks,
                        double relative_error, bool validate) {
  for (size_t dataset = 0; dataset < datasets.size(); ++dataset) {
    Decompressor decompressor;
    size_t original_index = 0;
    for (const Array<uint8_t> &block : blocks[dataset]) {
      const std::vector<double> recovered = decompressor.Decompress(block);
      if (validate) {
        for (double value : recovered) {
          if (!WithinRelativeError(datasets[dataset][original_index++], value, relative_error)) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

template <typename Compressor, typename Decompressor>
BenchmarkResult Benchmark(const std::vector<std::vector<double>> &datasets, double relative_error) {
  BenchmarkResult result;
  std::vector<std::vector<Array<uint8_t>>> blocks;
  result.bits = CompressDatasets<Compressor>(datasets, relative_error, &blocks);
  for (const auto &values : datasets) {
    result.values += values.size() / kBlockSizeOverall * kBlockSizeOverall;
  }
  result.valid = DecompressDatasets<Decompressor>(datasets, blocks, relative_error, true);

  for (int repetition = -kWarmups; repetition < kRepetitions; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    const uint64_t bits = CompressDatasets<Compressor>(datasets, relative_error, nullptr);
    const auto end = std::chrono::steady_clock::now();
    if (bits != result.bits) throw std::runtime_error("Non-deterministic compressed size");
    if (repetition >= 0) {
      result.compression_ms +=
          std::chrono::duration<double, std::milli>(end - start).count() / kRepetitions;
    }
  }
  for (int repetition = -kWarmups; repetition < kRepetitions; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    DecompressDatasets<Decompressor>(datasets, blocks, relative_error, false);
    const auto end = std::chrono::steady_clock::now();
    if (repetition >= 0) {
      result.decompression_ms +=
          std::chrono::duration<double, std::milli>(end - start).count() / kRepetitions;
    }
  }
  return result;
}

void Print(double relative_error, const char *method, const BenchmarkResult &result) {
  std::cout << relative_error << ',' << method << ',' << result.values << ',' << result.bits << ','
            << static_cast<double>(result.bits) / (64.0 * result.values) << ','
            << result.compression_ms << ',' << result.decompression_ms << ','
            << (result.valid ? "true" : "false") << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  const bool zigzag_first = argc > 2 && std::string(argv[2]) == "zigzag-first";
  std::vector<std::vector<double>> datasets;
  for (const std::string &dataset : kDataSetList) datasets.push_back(ReadValues(dataset_dir / dataset));

  std::cout << std::setprecision(12);
  std::cout << "RelativeError,Method,Values,Bits,CompressionRatio,CompressionMs,DecompressionMs,Valid\n";
  for (double relative_error : kMaxDiffRel) {
    if (zigzag_first) {
      Print(relative_error, "ZigZag",
            Benchmark<ZigZagLogCompressor, ZigZagLogDecompressor>(datasets, relative_error));
      Print(relative_error, "ModeSign",
            Benchmark<LogSerfQtCompressor, LogSerfQtDecompressor>(datasets, relative_error));
    } else {
      Print(relative_error, "ModeSign",
            Benchmark<LogSerfQtCompressor, LogSerfQtDecompressor>(datasets, relative_error));
      Print(relative_error, "ZigZag",
            Benchmark<ZigZagLogCompressor, ZigZagLogDecompressor>(datasets, relative_error));
    }
  }
}
