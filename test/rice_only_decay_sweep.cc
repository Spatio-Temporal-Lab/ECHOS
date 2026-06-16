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
#include "utils/adaptive_qt_codec.h"
#include "utils/array.h"
#include "utils/double.h"
#include "utils/input_bit_stream.h"
#include "utils/output_bit_stream.h"

namespace {

constexpr uint32_t kRiceQuotientCap = 16;
constexpr uint64_t kEscape = std::numeric_limits<uint64_t>::max();
constexpr int kMinShift = 1;
constexpr int kMaxShift = 10;

struct State {
  uint64_t delta_cost = 0;
  uint64_t rice_cost = 0;
  uint64_t magnitude_sum = 0;
  uint64_t sample_count = 0;
};

struct Lengths {
  uint64_t delta = 0;
  uint64_t rice = 0;
};

struct Choice {
  AdaptiveQtCodec::IntegerCodec codec = AdaptiveQtCodec::IntegerCodec::kRice;
  uint32_t rice_parameter = 0;
};

struct Result {
  uint64_t values = 0;
  uint64_t bits = 0;
  double compression_ms = 0;
  double decompression_ms = 0;
  bool valid = true;
};

uint64_t DecayAndAdd(uint64_t cost, uint64_t value, uint32_t shift) {
  return AdaptiveQtCodec::SaturatingAdd(cost - (cost >> shift), value);
}

uint32_t EstimateRiceParameter(const State &state) {
  if (state.magnitude_sum == 0 || state.sample_count == 0) return 0;
  const uint32_t magnitude_log = AdaptiveQtCodec::FloorLog2(state.magnitude_sum);
  const uint32_t sample_log = AdaptiveQtCodec::FloorLog2(state.sample_count);
  return magnitude_log > sample_log ? magnitude_log - sample_log : 0;
}

Choice SelectDeltaRice(const State &state) {
  Choice choice{AdaptiveQtCodec::IntegerCodec::kDelta, EstimateRiceParameter(state)};
  if (state.rice_cost < state.delta_cost) choice.codec = AdaptiveQtCodec::IntegerCodec::kRice;
  return choice;
}

Choice SelectRiceOnly(const State &state) {
  return {AdaptiveQtCodec::IntegerCodec::kRice, EstimateRiceParameter(state)};
}

Lengths CalculateCappedRiceLengths(uint64_t mapped, uint32_t rice_parameter, bool raw_escape) {
  const uint64_t delta = AdaptiveQtCodec::DeltaLength(mapped);
  const uint64_t quotient = (mapped - 1) >> rice_parameter;
  const uint64_t rice_direct = quotient + rice_parameter + 1;
  return {delta,
          quotient < kRiceQuotientCap ? rice_direct
                                      : kRiceQuotientCap + static_cast<uint64_t>(raw_escape) +
                                            delta};
}

void UpdateState(uint64_t mapped, const Lengths &lengths, uint32_t shift, State *state) {
  state->delta_cost = DecayAndAdd(state->delta_cost, lengths.delta, shift);
  state->rice_cost = DecayAndAdd(state->rice_cost, lengths.rice, shift);
  state->magnitude_sum =
      DecayAndAdd(state->magnitude_sum,
                  std::min(mapped - 1, AdaptiveQtCodec::kAdaptiveRiceMagnitudeCap), shift);
  state->sample_count = DecayAndAdd(state->sample_count, 1, shift);
}

bool QuantizeAbsolute(double value, double prediction, double max_diff, double step,
                      double inverse_step, int64_t *q, double *recovered) {
  if (!std::isfinite(value) || !std::isfinite(prediction)) return false;
  const double scaled = (value - prediction) * inverse_step;
  if (std::isfinite(scaled) && std::abs(scaled) <= 0x1p52) {
    *q = static_cast<int64_t>(std::round(scaled));
    if (AdaptiveQtCodec::ZigZagEncode(*q) < kEscape - 1) {
      *recovered = prediction + step * static_cast<double>(*q);
      if (std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff) return true;
    }
  }

  const long double precise_step = 2.0L * static_cast<long double>(max_diff);
  const long double precise_scaled =
      (static_cast<long double>(value) - static_cast<long double>(prediction)) / precise_step;
  if (!std::isfinite(precise_scaled)) return false;
  const long double rounded = std::round(precise_scaled);
  if (rounded <= static_cast<long double>(std::numeric_limits<int64_t>::min()) ||
      rounded > static_cast<long double>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  *q = static_cast<int64_t>(rounded);
  if (AdaptiveQtCodec::ZigZagEncode(*q) >= kEscape - 1) return false;
  *recovered = prediction + step * static_cast<double>(*q);
  return std::isfinite(*recovered) && std::abs(value - *recovered) <= max_diff;
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

template <bool RiceOnly>
class AbsoluteCompressor {
 public:
  AbsoluteCompressor(int block_size, double requested_max_diff, uint32_t shift)
      : block_size_(block_size),
        max_diff_(requested_max_diff * 0.999),
        step_(2 * max_diff_),
        inverse_step_(1.0 / step_),
        shift_(shift),
        output_(std::make_unique<OutputBitStream>(static_cast<uint32_t>(24 * block_size + 32))) {}

  void AddValue(double value) {
    if (value_count_ == 0) WriteMetadata();
    const Choice choice = RiceOnly ? SelectRiceOnly(state_) : SelectDeltaRice(state_);
    int64_t q = 0;
    double recovered = 0;
    if (QuantizeAbsolute(value, previous_, max_diff_, step_, inverse_step_, &q, &recovered)) {
      const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q) + 1;
      const Lengths lengths =
          CalculateCappedRiceLengths(mapped, choice.rice_parameter, true);
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        bits_ += AdaptiveQtCodec::EncodeCappedRiceWithRaw(
            mapped, choice.rice_parameter, kRiceQuotientCap, output_.get());
      } else {
        bits_ += AdaptiveQtCodec::EncodeDelta(mapped, output_.get());
      }
      UpdateState(mapped, lengths, shift_, &state_);
      previous_ = recovered;
    } else {
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        bits_ += AdaptiveQtCodec::WriteCappedRiceRaw(kRiceQuotientCap, output_.get());
      } else {
        bits_ += AdaptiveQtCodec::EncodeDelta(kEscape, output_.get());
      }
      bits_ += output_->WriteLong(Double::DoubleToLongBits(value), 64);
      previous_ = std::isfinite(value) ? value : 2;
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
      bits_ += output_->WriteLong(Double::DoubleToLongBits(max_diff_), 64);
      metadata_initialized_ = true;
    }
  }

  int block_size_;
  double max_diff_;
  double step_;
  double inverse_step_;
  uint32_t shift_;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_ = 2;
  State state_{};
  bool metadata_initialized_ = false;
  int value_count_ = 0;
  long bits_ = 0;
  long stored_bits_ = 0;
};

template <bool RiceOnly>
class AbsoluteDecompressor {
 public:
  explicit AbsoluteDecompressor(uint32_t shift) : shift_(shift) {}

  std::vector<double> Decompress(const Array<uint8_t> &bytes) {
    InputBitStream input;
    input.SetBuffer(bytes);
    const bool block_size_changed = input.ReadBit();
    const bool max_diff_changed = input.ReadBit();
    if (block_size_changed) block_size_ = input.ReadInt(16);
    if (max_diff_changed) {
      max_diff_ = Double::LongBitsToDouble(input.ReadLong(64));
      step_ = 2 * max_diff_;
    }
    std::vector<double> result;
    result.reserve(block_size_);
    for (int index = 0; index < block_size_; ++index) {
      const Choice choice = RiceOnly ? SelectRiceOnly(state_) : SelectDeltaRice(state_);
      bool raw = false;
      uint64_t mapped;
      if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
        mapped = AdaptiveQtCodec::DecodeCappedRiceWithRaw(
            choice.rice_parameter, kRiceQuotientCap, &raw, &input);
      } else {
        mapped = AdaptiveQtCodec::DecodeDelta(&input);
        raw = mapped == kEscape;
      }

      double value;
      if (raw) {
        value = Double::LongBitsToDouble(input.ReadLong(64));
      } else {
        value = previous_ + step_ * static_cast<double>(AdaptiveQtCodec::ZigZagDecode(mapped - 1));
        UpdateState(mapped, CalculateCappedRiceLengths(mapped, choice.rice_parameter, true),
                    shift_, &state_);
      }
      result.push_back(value);
      previous_ = std::isfinite(value) ? value : 2;
    }
    return result;
  }

 private:
  uint32_t shift_;
  double previous_ = 2;
  State state_{};
  int block_size_ = 0;
  double max_diff_ = 0;
  double step_ = 0;
};

enum class LogMode {
  kRepeat,
  kSameSignResidual,
  kChangedSignResidual,
  kChangedSignZeroResidual,
  kZero,
  kRaw
};

LogMode ReadLogMode(InputBitStream *input) {
  if (!input->ReadBit()) return LogMode::kRepeat;
  if (!input->ReadBit()) return LogMode::kSameSignResidual;
  if (!input->ReadBit()) return LogMode::kChangedSignResidual;
  if (!input->ReadBit()) return LogMode::kChangedSignZeroResidual;
  return !input->ReadBit() ? LogMode::kZero : LogMode::kRaw;
}

template <bool RiceOnly>
class LogCompressor {
 public:
  LogCompressor(int block_size, double relative_error, uint32_t shift)
      : block_size_(block_size),
        relative_error_(relative_error),
        upper_error_(std::log1p(relative_error)),
        lower_error_(relative_error < 1 ? std::log1p(-relative_error)
                                        : -std::numeric_limits<double>::infinity()),
        log_max_diff_(upper_error_ * 0.999),
        inverse_step_(1.0 / (2 * log_max_diff_)),
        shift_(shift),
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

    const Choice choice = RiceOnly ? SelectRiceOnly(state_) : SelectDeltaRice(state_);
    int64_t q = 0;
    double target_log = 0;
    double recovered_log = 0;
    const bool quantized =
        QuantizeLog(magnitude, previous_log_, log_max_diff_, inverse_step_, lower_error_,
                    upper_error_, &q, &target_log, &recovered_log);
    if (quantized && q != 0) {
      const uint64_t mapped = AdaptiveQtCodec::ZigZagEncode(q);
      const Lengths lengths = CalculateCappedRiceLengths(mapped, choice.rice_parameter, false);
      const uint64_t prefix_bits = sign == previous_sign_ ? 2 : 3;
      const uint64_t residual_bits =
          choice.codec == AdaptiveQtCodec::IntegerCodec::kRice ? lengths.rice : lengths.delta;
      if (prefix_bits + residual_bits < 69) {
        bits_ += sign == previous_sign_ ? output_->WriteInt(2, 2) : output_->WriteInt(6, 3);
        if (choice.codec == AdaptiveQtCodec::IntegerCodec::kRice) {
          bits_ += AdaptiveQtCodec::EncodeCappedRice(
              mapped, choice.rice_parameter, kRiceQuotientCap, output_.get());
        } else {
          bits_ += AdaptiveQtCodec::EncodeDelta(mapped, output_.get());
        }
        UpdateState(mapped, lengths, shift_, &state_);
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
  uint32_t shift_;
  std::unique_ptr<OutputBitStream> output_;
  Array<uint8_t> compressed_bytes_;
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  State state_{};
  bool metadata_initialized_ = false;
  int value_count_ = 0;
  long bits_ = 0;
  long stored_bits_ = 0;
};

template <bool RiceOnly>
class LogDecompressor {
 public:
  explicit LogDecompressor(uint32_t shift) : shift_(shift) {}

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
      const LogMode mode = ReadLogMode(&input);
      if (mode == LogMode::kRaw) {
        const double value = Double::LongBitsToDouble(input.ReadLong(64));
        result.push_back(value);
        if (std::isfinite(value) && value != 0) {
          previous_log_ = std::log(std::abs(value));
          previous_value_ = value;
          previous_sign_ = std::signbit(value);
        }
        continue;
      }
      if (mode == LogMode::kZero) {
        result.push_back(0.0);
        continue;
      }
      if (mode == LogMode::kRepeat) {
        result.push_back(previous_value_);
        continue;
      }
      if (mode == LogMode::kChangedSignZeroResidual) {
        previous_sign_ = !previous_sign_;
        previous_value_ = previous_sign_ ? -std::abs(previous_value_) : std::abs(previous_value_);
        result.push_back(previous_value_);
        continue;
      }

      const Choice choice = RiceOnly ? SelectRiceOnly(state_) : SelectDeltaRice(state_);
      const uint64_t mapped =
          choice.codec == AdaptiveQtCodec::IntegerCodec::kRice
              ? AdaptiveQtCodec::DecodeCappedRice(choice.rice_parameter, kRiceQuotientCap, &input)
              : AdaptiveQtCodec::DecodeDelta(&input);
      if (mode == LogMode::kChangedSignResidual) previous_sign_ = !previous_sign_;
      const int64_t q = AdaptiveQtCodec::ZigZagDecode(mapped);
      previous_log_ += 2 * log_max_diff_ * static_cast<double>(q);
      const double magnitude = std::exp(previous_log_);
      previous_value_ = previous_sign_ ? -magnitude : magnitude;
      result.push_back(previous_value_);
      UpdateState(mapped, CalculateCappedRiceLengths(mapped, choice.rice_parameter, false),
                  shift_, &state_);
    }
    return result;
  }

 private:
  uint32_t shift_;
  double previous_log_ = 0;
  double previous_value_ = 1;
  bool previous_sign_ = false;
  State state_{};
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
                          uint32_t shift, std::vector<std::vector<Array<uint8_t>>> *blocks) {
  uint64_t bits = 0;
  if (blocks != nullptr) blocks->clear();
  for (const std::vector<double> &values : datasets) {
    Compressor compressor(kBlockSizeOverall, error, shift);
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

template <typename Decompressor, bool Relative>
bool DecompressDatasets(const std::vector<std::vector<double>> &datasets,
                        const std::vector<std::vector<Array<uint8_t>>> &blocks, double error,
                        uint32_t shift) {
  for (size_t dataset = 0; dataset < datasets.size(); ++dataset) {
    Decompressor decompressor(shift);
    size_t original_index = 0;
    for (const Array<uint8_t> &block : blocks[dataset]) {
      const std::vector<double> recovered = decompressor.Decompress(block);
      for (double value : recovered) {
        const double original = datasets[dataset][original_index++];
        if constexpr (Relative) {
          if (original == 0) {
            if (value != 0 || std::signbit(value)) return false;
          } else if (std::signbit(original) != std::signbit(value) ||
                     std::abs(original - value) / std::abs(original) > error) {
            return false;
          }
        } else if (std::abs(original - value) > error) {
          return false;
        }
      }
    }
  }
  return true;
}

template <typename Compressor, typename Decompressor, bool Relative>
Result Benchmark(const std::vector<std::vector<double>> &datasets, double error, uint32_t shift) {
  Result result;
  std::vector<std::vector<Array<uint8_t>>> blocks;
  const auto compression_start = std::chrono::steady_clock::now();
  result.bits = CompressDatasets<Compressor>(datasets, error, shift, &blocks);
  const auto compression_end = std::chrono::steady_clock::now();
  for (const std::vector<double> &values : datasets) {
    result.values += values.size() / kBlockSizeOverall * kBlockSizeOverall;
  }
  const auto decompression_start = std::chrono::steady_clock::now();
  result.valid = DecompressDatasets<Decompressor, Relative>(datasets, blocks, error, shift);
  const auto decompression_end = std::chrono::steady_clock::now();
  result.compression_ms =
      std::chrono::duration<double, std::milli>(compression_end - compression_start).count();
  result.decompression_ms =
      std::chrono::duration<double, std::milli>(decompression_end - decompression_start).count();
  return result;
}

void Print(const char *domain, double error, const char *variant, uint32_t shift,
           const Result &result) {
  std::cout << domain << ',' << error << ',' << variant << ',' << shift << ',' << result.values
            << ',' << result.bits << ',' << static_cast<double>(result.bits) / (64.0 * result.values)
            << ',' << result.compression_ms << ',' << result.decompression_ms << ','
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
  std::cout << "Domain,Error,Variant,Shift,Values,Bits,CompressionRatio,CompressionMs,"
               "DecompressionMs,Valid\n";
  for (double error : kMaxDiffList) {
    for (uint32_t shift = kMinShift; shift <= kMaxShift; ++shift) {
      Print("Absolute", error, "Delta+Rice", shift,
            Benchmark<AbsoluteCompressor<false>, AbsoluteDecompressor<false>, false>(
                datasets, error, shift));
      Print("Absolute", error, "RiceOnly16", shift,
            Benchmark<AbsoluteCompressor<true>, AbsoluteDecompressor<true>, false>(
                datasets, error, shift));
    }
  }
  for (double error : kMaxDiffRel) {
    for (uint32_t shift = kMinShift; shift <= kMaxShift; ++shift) {
      Print("RelativeZigZag", error, "Delta+Rice", shift,
            Benchmark<LogCompressor<false>, LogDecompressor<false>, true>(datasets, error, shift));
      Print("RelativeZigZag", error, "RiceOnly16", shift,
            Benchmark<LogCompressor<true>, LogDecompressor<true>, true>(datasets, error, shift));
    }
  }
}
