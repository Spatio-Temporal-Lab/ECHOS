#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "Perf_expr_config.hpp"
#include "compressor/log_serf_qt_zigzag_compressor.h"
#include "decompressor/log_serf_qt_zigzag_decompressor.h"
#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/double.h"
#include "utils/input_bit_stream.h"
#include "utils/output_bit_stream.h"

namespace {

constexpr uint32_t kRiceQuotientCap = 16;
constexpr int kWarmups = 1;
constexpr int kRepetitions = 5;

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  double compression_ms = 0;
  double decompression_ms = 0;
  bool valid = true;
};

enum class Mode {
  kRepeat,
  kSameSignResidual,
  kChangedSignResidual,
  kChangedSignZeroResidual,
  kZero,
  kRaw
};

Mode ReadMode(InputBitStream *input) {
  if (!input->ReadBit()) return Mode::kRepeat;
  if (!input->ReadBit()) return Mode::kSameSignResidual;
  if (!input->ReadBit()) return Mode::kChangedSignResidual;
  if (!input->ReadBit()) return Mode::kChangedSignZeroResidual;
  return !input->ReadBit() ? Mode::kZero : Mode::kRaw;
}

bool QuantizeLog(double magnitude, double prediction, double log_max_diff, double inverse_step,
                 double lower_error, double upper_error, int64_t *q, double *target_log,
                 double *recovered_log) {
  if (!std::isfinite(magnitude) || magnitude == 0) return false;
  *target_log = std::log(magnitude);
  const double scaled = (*target_log - prediction) * inverse_step;
  if (!std::isfinite(scaled)) return false;
  if (std::abs(scaled) > 0x1p52) {
    const long double step = 2.0L * static_cast<long double>(log_max_diff);
    const long double precise_scaled =
        (static_cast<long double>(*target_log) - static_cast<long double>(prediction)) / step;
    if (!std::isfinite(precise_scaled)) return false;
    const long double rounded = std::round(precise_scaled);
    if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
        rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
    *q = static_cast<int64_t>(rounded);
  } else {
    *q = static_cast<int64_t>(std::round(scaled));
  }
  if (*q == std::numeric_limits<int64_t>::min()) return false;
  *recovered_log = prediction + 2 * log_max_diff * static_cast<double>(*q);
  const double error = *recovered_log - *target_log;
  return error >= lower_error && error <= upper_error;
}

uint64_t DeltaLength(uint64_t mapped) {
  const uint64_t value_bits = AdaptiveQtCodec::FloorLog2(mapped) + 1;
  return AdaptiveQtCodec::GammaLength(value_bits) + value_bits - 1;
}

AdaptiveQtCodec::AdaptiveDeltaRiceCodeLengths Lengths(uint64_t mapped, uint32_t rice_parameter) {
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t rice_direct = quotient + rice_parameter + 1;
  return {DeltaLength(mapped),
          quotient < kRiceQuotientCap ? rice_direct : kRiceQuotientCap + DeltaLength(mapped)};
}

class LogSerfQtZigZagRiceOnlyCompressor {
 public:
  LogSerfQtZigZagRiceOnlyCompressor(int block_size, double relative_error)
      : block_size_(block_size),
        relative_error_(relative_error),
        upper_error_(std::log1p(relative_error)),
        lower_error_(relative_error < 1 ? std::log1p(-relative_error)
                                        : -std::numeric_limits<double>::infinity()),
        log_max_diff_(upper_error_ * 0.999),
        inverse_step_(1.0 / (2 * log_max_diff_)),
        output_(std::make_unique<OutputBitStream>(static_cast<uint32_t>(16 * block_size + 32))) {}

  void AddValue(double value) {
    if (value_count_ == 0) WriteMetadata();
    const bool sign = std::signbit(value);
    if (value == 0) {
      bits_ += output_->WriteInt(30, 5);
      ++value_count_;
      return;
    }
    const double magnitude = std::abs(value);
    if (std::isfinite(value) && sign == previous_sign_ &&
        std::abs(previous_value_ - value) <= relative_error_ * magnitude) {
      bits_ += output_->WriteBit(false);
      ++value_count_;
      return;
    }
    if (std::isfinite(value) && sign != previous_sign_ &&
        std::abs(std::abs(previous_value_) - magnitude) <= relative_error_ * magnitude) {
      bits_ += output_->WriteInt(14, 4);
      previous_sign_ = sign;
      previous_value_ = sign ? -std::abs(previous_value_) : std::abs(previous_value_);
      ++value_count_;
      return;
    }

    int64_t q = 0;
    double target_log = 0;
    double recovered_log = 0;
    const bool quantized =
        QuantizeLog(magnitude, previous_log_, log_max_diff_, inverse_step_, lower_error_,
                    upper_error_, &q, &target_log, &recovered_log);
    if (quantized && q != 0) {
      const uint32_t rice_parameter = AdaptiveQtCodec::EstimateRiceParameter(state_);
      const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q);
      const AdaptiveQtCodec::AdaptiveDeltaRiceCodeLengths lengths = Lengths(mapped, rice_parameter);
      const uint64_t prefix_bits = sign == previous_sign_ ? 2 : 3;
      if (prefix_bits + lengths.rice < 69) {
        bits_ += sign == previous_sign_ ? output_->WriteInt(2, 2) : output_->WriteInt(6, 3);
        bits_ += AdaptiveQtCodec::EncodeCappedRice(
            mapped, rice_parameter, kRiceQuotientCap, output_.get());
        AdaptiveQtCodec::UpdateAdaptiveDeltaRiceState(mapped, lengths, &state_);
        previous_log_ = recovered_log;
        const double recovered_magnitude = std::exp(recovered_log);
        previous_value_ = sign ? -recovered_magnitude : recovered_magnitude;
        previous_sign_ = sign;
        ++value_count_;
        return;
      }
    }

    bits_ += output_->WriteInt(31, 5);
    bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
    if (std::isfinite(value) && value != 0) {
      previous_log_ = quantized ? target_log : std::log(magnitude);
      previous_value_ = value;
      previous_sign_ = sign;
    }
    ++value_count_;
  }

  void Close() {
    output_->Flush();
    compressed_bytes_ = output_->GetBuffer(static_cast<uint32_t>(std::ceil(bits_ / 8.0)));
    output_->Refresh();
    stored_bits_ = bits_;
    bits_ = 0;
    value_count_ = 0;
  }

  Array<uint8_t> compressed_bytes() const { return compressed_bytes_; }
  long get_compressed_size_in_bits() const { return stored_bits_; }

 private:
  void WriteMetadata() {
    bits_ += output_->WriteBit(!metadata_initialized_);
    bits_ += output_->WriteBit(!metadata_initialized_);
    if (!metadata_initialized_) {
      bits_ += output_->WriteInt(block_size_, 16);
      bits_ += output_->WriteLong(Double::DoubleToLongBits(log_max_diff_), 64);
      metadata_initialized_ = true;
    }
  }

  int block_size_;
  double relative_error_;
  double upper_error_;
  double lower_error_;
  double log_max_diff_;
  double inverse_step_;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  AdaptiveQtCodec::AdaptiveDeltaRiceState state_{};
  bool metadata_initialized_ = false;
  int value_count_ = 0;
  long bits_ = 0;
  long stored_bits_ = 0;
};

class LogSerfQtZigZagRiceOnlyDecompressor {
 public:
  std::vector<double> Decompress(const Array<uint8_t> &bytes) {
    InputBitStream input;
    input.SetBuffer(bytes);
    const bool block_size_changed = input.ReadBit();
    const bool log_max_diff_changed = input.ReadBit();
    if (block_size_changed) block_size_ = input.ReadInt(16);
    if (log_max_diff_changed) log_max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
    std::vector<double> result;
    result.reserve(block_size_);
    for (int index = 0; index < block_size_; ++index) {
      const Mode mode = ReadMode(&input);
      if (mode == Mode::kRaw) {
        const double value = Double::LongBitsToDouble(input.ReadLong(64));
        result.push_back(value);
        if (std::isfinite(value) && value != 0) {
          previous_log_ = std::log(std::abs(value));
          previous_value_ = value;
          previous_sign_ = std::signbit(value);
        }
        continue;
      }
      if (mode == Mode::kZero) {
        result.push_back(0.0);
        continue;
      }
      if (mode == Mode::kRepeat) {
        result.push_back(previous_value_);
        continue;
      }
      if (mode == Mode::kChangedSignZeroResidual) {
        previous_sign_ = !previous_sign_;
        previous_value_ = previous_sign_ ? -std::abs(previous_value_) : std::abs(previous_value_);
        result.push_back(previous_value_);
        continue;
      }

      const uint32_t rice_parameter = AdaptiveQtCodec::EstimateRiceParameter(state_);
      const uint64_t mapped =
          AdaptiveQtCodec::DecodeCappedRice(rice_parameter, kRiceQuotientCap, &input);
      if (mode == Mode::kChangedSignResidual) previous_sign_ = !previous_sign_;
      const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped);
      previous_log_ += 2 * log_max_diff_ * static_cast<double>(q);
      const double magnitude = std::exp(previous_log_);
      previous_value_ = previous_sign_ ? -magnitude : magnitude;
      result.push_back(previous_value_);
      AdaptiveQtCodec::UpdateAdaptiveDeltaRiceState(mapped, Lengths(mapped, rice_parameter),
                                                    &state_);
    }
    return result;
  }

 private:
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  AdaptiveQtCodec::AdaptiveDeltaRiceState state_{};
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
uint64_t CompressDatasets(const std::vector<std::vector<double>> &datasets, double error,
                          std::vector<std::vector<Array<uint8_t>>> *blocks) {
  uint64_t bits = 0;
  if (blocks != nullptr) blocks->clear();
  for (const std::vector<double> &values : datasets) {
    Compressor compressor(kBlockSizeOverall, error);
    if (blocks != nullptr) blocks->emplace_back();
    for (size_t begin = 0; begin + kBlockSizeOverall <= values.size(); begin += kBlockSizeOverall) {
      for (size_t i = begin; i < begin + kBlockSizeOverall; ++i) compressor.AddValue(values[i]);
      compressor.Close();
      bits += compressor.get_compressed_size_in_bits();
      if (blocks != nullptr) blocks->back().push_back(compressor.compressed_bytes());
    }
  }
  return bits;
}

template <typename Decompressor>
bool DecompressDatasets(const std::vector<std::vector<double>> &datasets,
                        const std::vector<std::vector<Array<uint8_t>>> &blocks, double error,
                        bool validate) {
  for (size_t dataset = 0; dataset < datasets.size(); ++dataset) {
    Decompressor decompressor;
    size_t original_index = 0;
    for (const Array<uint8_t> &block : blocks[dataset]) {
      const std::vector<double> recovered = decompressor.Decompress(block);
      if (!validate) continue;
      for (double value : recovered) {
        const double original = datasets[dataset][original_index++];
        if (original == 0) {
          if (value != 0 || std::signbit(value)) return false;
        } else if (std::signbit(original) != std::signbit(value) ||
                   std::abs(original - value) / std::abs(original) > error) {
          return false;
        }
      }
    }
  }
  return true;
}

template <typename Compressor, typename Decompressor>
Result Benchmark(const std::vector<std::vector<double>> &datasets, double error) {
  Result result;
  std::vector<std::vector<Array<uint8_t>>> blocks;
  result.bits = CompressDatasets<Compressor>(datasets, error, &blocks);
  for (const std::vector<double> &values : datasets) {
    result.values += values.size() / kBlockSizeOverall * kBlockSizeOverall;
  }
  result.valid = DecompressDatasets<Decompressor>(datasets, blocks, error, true);

  for (int repetition = -kWarmups; repetition < kRepetitions; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    const uint64_t bits = CompressDatasets<Compressor>(datasets, error, nullptr);
    const auto end = std::chrono::steady_clock::now();
    if (bits != result.bits) throw std::runtime_error("Non-deterministic compressed size");
    if (repetition >= 0) {
      result.compression_ms +=
          std::chrono::duration<double, std::milli>(end - start).count() / kRepetitions;
    }
  }
  for (int repetition = -kWarmups; repetition < kRepetitions; ++repetition) {
    const auto start = std::chrono::steady_clock::now();
    DecompressDatasets<Decompressor>(datasets, blocks, error, false);
    const auto end = std::chrono::steady_clock::now();
    if (repetition >= 0) {
      result.decompression_ms +=
          std::chrono::duration<double, std::milli>(end - start).count() / kRepetitions;
    }
  }
  return result;
}

void Print(double error, const char *variant, const Result &result) {
  std::cout << error << ',' << variant << ',' << result.values << ',' << result.bits << ','
            << static_cast<double>(result.bits) / (64.0 * result.values) << ','
            << result.compression_ms << ',' << result.decompression_ms << ','
            << (result.valid ? "true" : "false") << '\n';
}

}  // namespace

int main(int argc, char **argv) {
  const std::filesystem::path dataset_dir =
      argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("test/data_set");
  std::vector<std::vector<double>> datasets;
  for (const std::string &dataset : kDataSetList) {
    datasets.push_back(ReadValues(dataset_dir / dataset));
  }

  std::cout << std::setprecision(12);
  std::cout << "Error,Variant,Values,Bits,CompressionRatio,CompressionMs,DecompressionMs,Valid\n";
  for (double error : kMaxDiffRel) {
    Print(error, "LogSerfQt-ZigZag",
          Benchmark<LogSerfQtZigZagCompressor, LogSerfQtZigZagDecompressor>(datasets, error));
    Print(error, "LogSerfQt-ZigZag-RiceOnly16",
          Benchmark<LogSerfQtZigZagRiceOnlyCompressor, LogSerfQtZigZagRiceOnlyDecompressor>(
              datasets, error));
  }
}
